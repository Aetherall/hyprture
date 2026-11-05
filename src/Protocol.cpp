// hyprture_v1 (protocol/hyprture-v1.xml): clients' apertures, on their own
// surfaces. Requests fill a pending state that the parent surface's commit
// applies.

#include "Protocol.hpp"
#include "Aperture.hpp"

#include <hyprland/src/protocols/WaylandProtocol.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include "hyprture-v1.hpp"

#include <cmath>

namespace {
    class CProtoAperture {
      public:
        CProtoAperture(UP<CHyprtureApertureV1>&& resource, SP<CWLSurfaceResource> surface);
        ~CProtoAperture();

        bool good() const {
            return m_resource && m_resource->resource();
        }

        UP<CHyprtureApertureV1> m_resource;

      private:
        SP<Aperture::CAperture> m_aperture;
        Aperture::SState        m_pending;

        struct {
            CHyprSignalListener commit;
            CHyprSignalListener destroy;
        } m_listeners;
    };

    class CHyprtureProtocol : public IWaylandProtocol {
      public:
        CHyprtureProtocol() : IWaylandProtocol(&hyprture_manager_v1_interface, 1, "hyprture") {}

        void bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id) override {
            const auto MANAGER = m_managers.emplace_back(makeUnique<CHyprtureManagerV1>(client, ver, id)).get();
            if (!MANAGER->resource()) {
                wl_client_post_no_memory(client);
                m_managers.pop_back();
                return;
            }
            MANAGER->setOnDestroy([this](CHyprtureManagerV1* m) { destroyManager(m); });
            MANAGER->setDestroy([this](CHyprtureManagerV1* m) { destroyManager(m); });
            MANAGER->setGetAperture([this](CHyprtureManagerV1* m, uint32_t id, wl_resource* surface) {
                auto aperture = makeUnique<CProtoAperture>(makeUnique<CHyprtureApertureV1>(m->client(), m->version(), id), CWLSurfaceResource::fromResource(surface));
                if (!aperture->good()) {
                    m->noMemory();
                    return;
                }
                m_apertures.emplace_back(std::move(aperture));
            });
        }

        void destroyManager(CHyprtureManagerV1* manager) {
            std::erase_if(m_managers, [manager](const auto& m) { return m.get() == manager; });
        }

        void destroyAperture(CHyprtureApertureV1* resource) {
            std::erase_if(m_apertures, [resource](const auto& a) { return a->m_resource.get() == resource; });
        }

      private:
        std::vector<UP<CHyprtureManagerV1>> m_managers;
        std::vector<UP<CProtoAperture>>     m_apertures;
    };

    UP<CHyprtureProtocol> g_protocol;

    CProtoAperture::CProtoAperture(UP<CHyprtureApertureV1>&& resource, SP<CWLSurfaceResource> surface) : m_resource(std::move(resource)) {
        if (!good() || !surface)
            return;

        m_aperture               = Aperture::create({.surface = surface});
        m_pending                = m_aperture->state;
        m_aperture->onSourceSize = [this](Vector2D size) {
            if (m_resource && m_resource->resource())
                m_resource->sendSource(sc<int32_t>(std::round(size.x)), sc<int32_t>(std::round(size.y)));
        };

        m_resource->setDestroy([](CHyprtureApertureV1* r) { g_protocol->destroyAperture(r); });
        m_resource->setOnDestroy([](CHyprtureApertureV1* r) { g_protocol->destroyAperture(r); });

        m_resource->setSetSourceWorkspace([this](CHyprtureApertureV1*, int32_t workspace, hyprtureApertureV1Scene scene) {
            m_pending.source = {.kind      = Aperture::SSource::WORKSPACE,
                                .workspace = workspace,
                                .scene     = scene == HYPRTURE_APERTURE_V1_SCENE_SHELL ? Aperture::eScene::SHELL : Aperture::eScene::WINDOWS};
        });
        m_resource->setSetSourceWindow([this](CHyprtureApertureV1*, const char* address) {
            m_pending.source = {.kind = Aperture::SSource::WINDOW, .window = address ? address : ""};
        });
        m_resource->setSetSourceMonitor([this](CHyprtureApertureV1*, const char* name) {
            m_pending.source = {.kind = Aperture::SSource::MONITOR, .monitor = name ? name : ""};
        });
        m_resource->setUnsetSource([this](CHyprtureApertureV1*) { m_pending.source = {}; });
        m_resource->setSetDestination([this](CHyprtureApertureV1* r, int32_t x, int32_t y, int32_t w, int32_t h) {
            if (w < 0 || h < 0) {
                r->error(HYPRTURE_APERTURE_V1_ERROR_INVALID_DESTINATION, "negative width or height");
                return;
            }
            m_pending.destination = {sc<double>(x), sc<double>(y), sc<double>(w), sc<double>(h)};
        });
        m_resource->setSetOpacity([this](CHyprtureApertureV1*, wl_fixed_t opacity) { m_pending.opacity = std::clamp(sc<float>(wl_fixed_to_double(opacity)), 0.F, 1.F); });
        m_resource->setSetRadius([this](CHyprtureApertureV1*, wl_fixed_t radius) { m_pending.radius = std::max(0.F, sc<float>(wl_fixed_to_double(radius))); });
        m_resource->setSetLive([this](CHyprtureApertureV1*, uint32_t live) { m_pending.live = live != 0; });

        m_listeners.commit = surface->m_events.commit.listen([this] {
            if (m_aperture)
                m_aperture->set(m_pending);
        });
        m_listeners.destroy = surface->m_events.destroy.listen([this] {
            // Inert from now on: the client destroys the object.
            m_listeners.commit.reset();
            if (m_aperture)
                Aperture::destroy(m_aperture->id);
            m_aperture.reset();
        });
    }

    CProtoAperture::~CProtoAperture() {
        if (m_aperture)
            Aperture::destroy(m_aperture->id);
    }
}

void Protocol::init() {
    g_protocol = makeUnique<CHyprtureProtocol>();
}

void Protocol::exit() {
    // Clients learn the global is gone before their objects go: they drop
    // theirs without sending anything to the objects destroyed here.
    if (g_protocol)
        g_protocol->removeGlobal();
    g_protocol.reset();
}
