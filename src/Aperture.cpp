#include "Aperture.hpp"

#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>

#include <format>

namespace Aperture {
    namespace {
        std::vector<SP<CAperture>> g_apertures;
        uint64_t                   g_nextID = 1;

        PHLMONITOR monitorNamed(const std::string& name) {
            for (const auto& mon : State::monitorState()->monitors()) {
                if (mon && mon->m_name == name)
                    return mon;
            }
            return nullptr;
        }

        PHLWORKSPACE workspaceNumbered(int64_t id) {
            for (const auto& ws : State::workspaceState()->workspaces()) {
                if (const auto N = ws->numberedID(); N && sc<int64_t>(*N) == id)
                    return ws.lock();
            }
            return nullptr;
        }

        PHLWINDOW windowAt(const std::string& address) {
            for (const auto& w : Desktop::windowState()->windows()) {
                if (w && std::format("0x{:x}", rc<uintptr_t>(w.get())) == address)
                    return w;
            }
            return nullptr;
        }

        PHLLS layerOf(const SP<CWLSurfaceResource>& surface) {
            const auto WL = surface ? Desktop::View::CWLSurface::fromResource(surface) : nullptr;
            return WL ? Desktop::View::CLayerSurface::fromView(WL->view()) : nullptr;
        }

        CBox toBox(const Fit::SBox& b) {
            return {b.x, b.y, b.w, b.h};
        }
    }

    CAperture::CAperture(STarget target_) : id(g_nextID++), target(std::move(target_)) {
        ;
    }

    CAperture::~CAperture() {
        damage();
    }

    void CAperture::set(const SState& next) {
        if (next == state)
            return;
        damage();
        const bool SOURCE = next.source != state.source;
        state             = next;
        dirty             = true;
        if (SOURCE)
            reportSourceSize(resolve().transform([](const auto& r) { return r.size; }).value_or(Vector2D{}));
        damage();
    }

    std::optional<SResolved> CAperture::resolve() const {
        const auto& S = state.source;
        SResolved   r;
        r.scene = S.scene;
        switch (S.kind) {
            case SSource::NONE: return std::nullopt;
            case SSource::WORKSPACE: r.workspace = workspaceNumbered(S.workspace); break;
            case SSource::MONITOR: {
                const auto MON = monitorNamed(S.monitor);
                r.workspace    = MON ? MON->m_activeWorkspace : nullptr;
                r.scene        = eScene::SHELL;
                break;
            }
            case SSource::WINDOW: {
                r.window = windowAt(S.window);
                if (!r.window || !r.window->mapped())
                    return std::nullopt;
                r.monitor = r.window->m_monitor.lock();
                r.size    = r.window->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
                return r.monitor ? std::optional{r} : std::nullopt;
            }
        }
        if (!r.workspace)
            return std::nullopt;
        r.monitor = r.workspace->m_monitor.lock();
        if (!r.monitor)
            return std::nullopt;
        r.size = r.monitor->m_size;
        return r;
    }

    PHLMONITOR CAperture::targetMonitor() const {
        if (const auto SURFACE = target.surface.lock()) {
            const auto LS = layerOf(SURFACE);
            return LS && LS->mapped() ? LS->m_monitor.lock() : nullptr;
        }
        return target.monitor.empty() ? nullptr : monitorNamed(target.monitor);
    }

    std::optional<CAperture::SPlacement> CAperture::placement() const {
        if (state.destination.empty() || state.opacity <= 0.F || sourceSize.x <= 0 || sourceSize.y <= 0)
            return std::nullopt;

        SPlacement p;
        Fit::SBox  bounds;
        if (const auto SURFACE = target.surface.lock()) {
            const auto LS = layerOf(SURFACE);
            if (!LS || !LS->mapped())
                return std::nullopt;
            p.monitor        = LS->m_monitor.lock();
            const auto POS   = LS->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
            const auto SIZE  = LS->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
            bounds           = {POS.x, POS.y, SIZE.x, SIZE.y};
            p.alpha          = LS->alpha()[Desktop::View::LS_ALPHA_FADE]->value();
        } else if (!target.monitor.empty()) {
            p.monitor = monitorNamed(target.monitor);
            if (p.monitor)
                bounds = {p.monitor->m_position.x, p.monitor->m_position.y, p.monitor->m_size.x, p.monitor->m_size.y};
        }
        if (!p.monitor || bounds.empty())
            return std::nullopt;

        const auto FITTED = Fit::contain(Fit::offset(state.destination, bounds.x, bounds.y), sourceSize.x, sourceSize.y);
        if (Fit::intersect(FITTED, bounds).empty())
            return std::nullopt;
        p.box  = toBox(FITTED);
        p.clip = toBox(bounds);
        p.alpha *= state.opacity;
        return p;
    }

    void CAperture::damage() {
        if (lastDrawn)
            g_pHyprRenderer->damageBox(*lastDrawn);
        if (const auto P = placement())
            g_pHyprRenderer->damageBox(P->box);
    }

    void CAperture::reportSourceSize(const Vector2D& size) {
        if (size == sourceSize)
            return;
        damage();
        sourceSize = size;
        dirty      = true;
        damage();
        if (onSourceSize)
            onSourceSize(size);
    }

    SP<CAperture> create(STarget target) {
        return g_apertures.emplace_back(makeShared<CAperture>(std::move(target)));
    }

    void destroy(uint64_t id) {
        std::erase_if(g_apertures, [id](const auto& a) { return a->id == id; });
    }

    SP<CAperture> find(uint64_t id) {
        const auto IT = std::ranges::find_if(g_apertures, [id](const auto& a) { return a->id == id; });
        return IT == g_apertures.end() ? nullptr : *IT;
    }

    const std::vector<SP<CAperture>>& all() {
        return g_apertures;
    }

    void clear() {
        g_apertures.clear();
    }
}
