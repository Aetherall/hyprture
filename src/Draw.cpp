// Drawing apertures. Before a monitor renders, each aperture shown on it whose
// view is live (or changed) renders its source offscreen, the way Hyprland's
// own workspace capture does: a workspace (or a window) into a framebuffer
// of its own. The monitor's frame then draws that framebuffer where the
// aperture is: right after its parent layer surface (a hook on renderLayer),
// or at a stage of the frame for apertures on a monitor. A live aperture
// damages itself after every frame, so the next one comes.

#include "Draw.hpp"
#include "Aperture.hpp"

#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Context.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/state/GlobalWindowController.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/cm/ColorManagement.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/SceneResources.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include <ranges>
#include <stdexcept>

using Hyprutils::Utils::CScopeGuard;

namespace Draw {
    namespace {
        HANDLE                          g_handle = nullptr;
        CFunctionHook*                  g_renderLayerHook = nullptr;
        std::vector<CHyprSignalListener> g_listeners;
        // While an aperture renders its source: apertures inside that scene
        // are not drawn (one could be drawing into the framebuffer it reads).
        bool g_offscreen = false;

        // The renderer's protected entry points: a whole offscreen frame, and a
        // workspace or a window drawn into it.
        struct SRendererAccess : Render::IHyprRenderer {
            using IHyprRenderer::beginRender;
            using IHyprRenderer::renderWindow;
            using IHyprRenderer::renderWorkspace;
        };
        using BeginRender     = bool (Render::IHyprRenderer::*)(PHLMONITOR, CRegion&, Render::eRenderMode, SP<IHLBuffer>, SP<Render::IFramebuffer>, bool,
                                                            const Render::SRenderOptions&, std::optional<Monitor::CDamageRing::CTransaction>*);
        using RenderWindow    = void (Render::IHyprRenderer::*)(Render::CRenderContext&, PHLWINDOW, PHLMONITOR, const Render::SWindowRenderPresentation&,
                                                             const Time::steady_tp&, bool, Render::eRenderPassMode, bool, bool);
        using RenderWorkspace = void (Render::IHyprRenderer::*)(Render::CRenderContext&, PHLWORKSPACE, const Time::steady_tp&, Render::eSceneMode);
        constexpr BeginRender     BEGIN_RENDER     = &SRendererAccess::beginRender;
        constexpr RenderWindow    RENDER_WINDOW    = &SRendererAccess::renderWindow;
        constexpr RenderWorkspace RENDER_WORKSPACE = &SRendererAccess::renderWorkspace;

        using origRenderLayer = void (*)(void*, Render::CRenderContext&, PHLLS, PHLMONITOR, const Time::steady_tp&, bool, bool, bool);

        bool locked() {
            return g_pSessionLockManager && g_pSessionLockManager->isSessionLocked();
        }

        // The source into the aperture's framebuffer.
        bool refresh(Aperture::CAperture& a, const Aperture::SResolved& src, const Time::steady_tp& now) {
            auto* const R = g_pHyprRenderer.get();
            if (R->context().active())
                return false;

            const auto MON    = src.monitor;
            const bool WINDOW = !!src.window;
            // A window renders alone at the origin of a framebuffer its size;
            // a workspace, as its monitor shows it.
            const Vector2D SIZE = WINDOW ? (src.size * MON->m_scale).round() : MON->m_transformedSize;
            if (SIZE.x < 1 || SIZE.y < 1)
                return false;

            if (!a.fb)
                a.fb = R->createFB("hyprture aperture");
            if (!a.fb->isAllocated() || a.fb->m_size != SIZE) {
                if (!a.fb->alloc(SIZE.x, SIZE.y, DRM_FORMAT_ARGB8888))
                    return false;
            }
            a.fb->setImageDescription(NColorManagement::DEFAULT_SRGB_IMAGE_DESCRIPTION);
            if (!a.sceneResources)
                a.sceneResources = makeShared<Render::CSceneResources>(R->createFB("hyprture aperture blur"),
                                                                       Render::SSceneBufferDescription{
                                                                           .format           = DRM_FORMAT_ARGB8888,
                                                                           .imageDescription = NColorManagement::DEFAULT_SRGB_IMAGE_DESCRIPTION,
                                                                       });

            Render::SRenderOptions options;
            options.sceneResources = a.sceneResources;
            CRegion damage{0, 0, sc<int>(SIZE.x), sc<int>(SIZE.y)};
            if (!(R->*BEGIN_RENDER)(MON, damage, Render::RENDER_MODE_FULL_FAKE, nullptr, a.fb, WINDOW, options, nullptr))
                return false;
            bool              finishing = false;
            const CScopeGuard cleanup([&] {
                if (!finishing)
                    R->abortRender();
            });

            auto& ctx = R->context();
            ctx.m_data.outFB->setImageDescription(NColorManagement::DEFAULT_SRGB_IMAGE_DESCRIPTION);
            ctx.m_data.blockScreenShader = true;
            ctx.m_renderingCapture       = true;
            ctx.m_blockSurfaceFeedback   = true;

            g_offscreen = true;
            const CScopeGuard offscreen([] { g_offscreen = false; });

            R->draw(ctx, CClearPassElement::SClearData{{0, 0, 0, 0}}, CRegion{0, 0, sc<int>(SIZE.x), sc<int>(SIZE.y)});
            R->startRenderPass(ctx);
            if (WINDOW)
                (R->*RENDER_WINDOW)(ctx, src.window, MON, src.window->presentation().renderPresentation(), now, false, Render::RENDER_PASS_ALL, true, true);
            else
                (R->*RENDER_WORKSPACE)(ctx, src.workspace, now,
                                       src.scene == Aperture::eScene::SHELL ? Render::eSceneMode::WORKSPACE_WITH_SHELL : Render::eSceneMode::WORKSPACE_WINDOWS);

            finishing = true;
            R->endRender();
            return true;
        }

        // A hidden workspace's clients get no frame callbacks (Hyprland skips
        // windows it doesn't show), so they stop drawing; a live view of them
        // sends them some.
        void tickSource(const Aperture::SResolved& src, const Time::steady_tp& now) {
            const auto TICK = [&now](const PHLWINDOW& w) {
                if (!w || !w->mapped() || !w->wlSurface() || !w->wlSurface()->resource())
                    return;
                w->wlSurface()->resource()->breadthfirst([&now](SP<CWLSurfaceResource> s, const Vector2D&, void*) { s->frame(now); }, nullptr);
            };
            if (src.window) {
                TICK(src.window);
                return;
            }
            for (const auto& w : Desktop::windowState()->windows()) {
                if (w->m_workspace == src.workspace)
                    TICK(w);
            }
        }

        // Hyprland suspends the windows of hidden workspaces (xdg_toplevel's
        // suspended state), and well-behaved clients stop drawing. The sources
        // of live apertures on screen stay awake; once none shows them,
        // Hyprland decides again.
        std::vector<PHLWINDOWREF> g_awake;

        void keepAwake() {
            std::vector<PHLWINDOWREF> wanted;
            for (const auto& a : Aperture::all()) {
                if (!a->state.live || !a->placement())
                    continue;
                const auto SRC = a->resolve();
                if (!SRC)
                    continue;
                if (SRC->window)
                    wanted.emplace_back(SRC->window);
                else {
                    for (const auto& w : Desktop::windowState()->windows()) {
                        if (w->mapped() && w->m_workspace == SRC->workspace)
                            wanted.emplace_back(w);
                    }
                }
            }
            for (const auto& ref : wanted) {
                if (const auto W = ref.lock())
                    W->setSuspended(false); // a no-op when awake already
            }
            const bool RELEASED = std::ranges::any_of(g_awake, [&wanted](const auto& ref) { return !std::ranges::contains(wanted, ref); });
            g_awake             = std::move(wanted);
            if (RELEASED)
                Desktop::globalWindowController()->updateSuspendedStates();
        }

        CBox toMonitor(const CBox& global, const PHLMONITOR& mon) {
            return CBox{global}.translate(-mon->m_position).scale(mon->m_scale);
        }

        void addView(Render::CRenderContext& ctx, Aperture::CAperture& a, const Aperture::CAperture::SPlacement& p, const PHLMONITOR& mon) {
            if (!a.fb || !a.fb->isAllocated())
                return;
            CTexPassElement::SRenderData data;
            data.tex           = a.fb->getTexture();
            data.box           = toMonitor(p.box, mon);
            data.clipBox       = toMonitor(p.clip, mon).round();
            data.a             = p.alpha;
            data.round         = sc<int>(std::round(a.state.radius * mon->m_scale));
            data.roundingPower = 2.F;
            Render::IHyprRenderer::addPassElement(ctx, makeUnique<CTexPassElement>(std::move(data)));
            a.lastDrawn = p.box;
        }

        bool drawable(const Render::CRenderContext& ctx) {
            return !g_offscreen && !ctx.m_renderingSnapshot && !locked();
        }

        void hkRenderLayer(void* thisptr, Render::CRenderContext& ctx, PHLLS layer, PHLMONITOR mon, const Time::steady_tp& now, bool popups, bool lockscreen,
                           bool workspaceScene) {
            rc<origRenderLayer>(g_renderLayerHook->m_original)(thisptr, ctx, layer, mon, now, popups, lockscreen, workspaceScene);
            if (popups || !layer || !mon || !drawable(ctx) || Aperture::all().empty())
                return;
            const auto SURFACE = layer->wlSurface() ? layer->wlSurface()->resource() : nullptr;
            if (!SURFACE)
                return;
            for (const auto& a : Aperture::all()) {
                if (a->target.surface != SURFACE)
                    continue;
                if (const auto P = a->placement(); P && P->monitor == mon)
                    addView(ctx, *a, *P, mon);
            }
        }

        void onPre(PHLMONITOR mon) {
            if (!g_awake.empty() || !Aperture::all().empty())
                keepAwake();
            if (Aperture::all().empty() || locked())
                return;
            const auto NOW = Time::steadyNow();
            // Copied: a refresh can't remove apertures, but stay safe.
            for (const auto& a : std::vector{Aperture::all()}) {
                if (a->targetMonitor() != mon)
                    continue;
                const auto SRC = a->resolve();
                a->reportSourceSize(SRC ? SRC->size : Vector2D{});
                if (!SRC || !a->placement())
                    continue;
                if (!a->state.live && !a->dirty && a->fb)
                    continue;
                if (refresh(*a, *SRC, NOW)) {
                    a->dirty = false;
                    if (a->state.live)
                        tickSource(*SRC, NOW);
                }
            }
        }

        void onStage(const Event::SRenderStageEvent& e) {
            if (Aperture::all().empty() || !e.monitor)
                return;

            if (e.stage == RENDER_POST) {
                for (const auto& a : Aperture::all()) {
                    if (!a->state.live)
                        continue;
                    if (const auto P = a->placement(); P && P->monitor == e.monitor)
                        g_pHyprRenderer->damageBox(P->box);
                }
                return;
            }

            const auto LEVEL = e.stage == RENDER_LAST_MOMENT ? Aperture::STarget::OVERLAY : e.stage == RENDER_POST_WINDOWS ? Aperture::STarget::WINDOWS : 0xFF;
            if (LEVEL == 0xFF || !e.context || !drawable(e.context->get()))
                return;
            // RENDER_POST_WINDOWS also comes from workspace captures; only the
            // monitor's own frame shows apertures on it.
            if (e.context->get().m_renderingCapture)
                return;
            for (const auto& a : Aperture::all()) {
                if (a->target.surface || a->target.level != LEVEL)
                    continue;
                if (const auto P = a->placement(); P && P->monitor == e.monitor)
                    addView(e.context->get(), *a, *P, e.monitor);
            }
        }

        void revalidate() {
            for (const auto& a : Aperture::all()) {
                const auto SRC = a->resolve();
                a->reportSourceSize(SRC ? SRC->size : Vector2D{});
                a->dirty = true;
                a->damage();
            }
        }

        void* findOrThrow(const std::string& name, const std::string& needle) {
            for (const auto& fn : HyprlandAPI::findFunctionsByName(g_handle, name)) {
                if (fn.demangled.find(needle) != std::string::npos)
                    return fn.address;
            }
            throw std::runtime_error("[hyprture] can't find " + needle);
        }
    }

    void init(HANDLE handle) {
        g_handle          = handle;
        g_renderLayerHook = HyprlandAPI::createFunctionHook(handle, findOrThrow("renderLayer", "IHyprRenderer::renderLayer("), rc<void*>(hkRenderLayer));
        if (!g_renderLayerHook || !g_renderLayerHook->hook())
            throw std::runtime_error("[hyprture] can't hook renderLayer");

        auto& ev = Event::bus()->m_events;
        g_listeners.push_back(ev.render.pre.listen(onPre));
        g_listeners.push_back(ev.render.stage.listen(onStage));
        g_listeners.push_back(ev.workspace.createdLate.listen([](PHLWORKSPACEREF) { revalidate(); }));
        g_listeners.push_back(ev.workspace.removed.listen([](PHLWORKSPACEREF) { revalidate(); }));
        g_listeners.push_back(ev.workspace.active.listen([](PHLWORKSPACE) { revalidate(); }));
        g_listeners.push_back(ev.window.open.listen([](PHLWINDOW) { revalidate(); }));
        g_listeners.push_back(ev.window.destroy.listen([](PHLWINDOWREF) { revalidate(); }));
        g_listeners.push_back(ev.window.moveToWorkspace.listen([](PHLWINDOW, PHLWORKSPACE) { revalidate(); }));
        g_listeners.push_back(ev.monitor.removed.listen([](PHLMONITOR) { revalidate(); }));
        g_listeners.push_back(ev.monitor.layoutChanged.listen([] { revalidate(); }));
    }

    void exit() {
        g_listeners.clear();
        if (!g_awake.empty()) {
            g_awake.clear();
            Desktop::globalWindowController()->updateSuspendedStates();
        }
        if (g_renderLayerHook)
            HyprlandAPI::removeFunctionHook(g_handle, g_renderLayerHook);
        g_renderLayerHook = nullptr;
    }
}
