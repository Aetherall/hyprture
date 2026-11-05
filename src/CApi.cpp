// The C API (hyprture.h), for other plugins.

#include "../hyprture.h"
#include "Aperture.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

namespace {
    SP<Aperture::CAperture> monitorAperture(uint64_t id) {
        const auto A = Aperture::find(id);
        return A && !A->target.surface ? A : nullptr;
    }
}

extern "C" {
EXPORT int hyprture_api_version() {
    return HYPRTURE_API_VERSION;
}

EXPORT uint64_t hyprture_create(const char* monitor, int level) {
    if (!monitor || !*monitor)
        return 0;
    Aperture::STarget target;
    target.monitor = monitor;
    target.level   = level == HYPRTURE_LEVEL_WINDOWS ? Aperture::STarget::WINDOWS : Aperture::STarget::OVERLAY;
    return Aperture::create(std::move(target))->id;
}

EXPORT bool hyprture_set_source(uint64_t id, const hyprture_source* source) {
    const auto A = monitorAperture(id);
    if (!A)
        return false;
    Aperture::SSource s;
    if (source) {
        switch (source->kind) {
            case HYPRTURE_SOURCE_WORKSPACE:
                s.kind      = Aperture::SSource::WORKSPACE;
                s.workspace = source->workspace;
                s.scene     = source->scene == HYPRTURE_SCENE_SHELL ? Aperture::eScene::SHELL : Aperture::eScene::WINDOWS;
                break;
            case HYPRTURE_SOURCE_WINDOW:
                s.kind   = Aperture::SSource::WINDOW;
                s.window = source->window ? source->window : "";
                break;
            case HYPRTURE_SOURCE_MONITOR:
                s.kind    = Aperture::SSource::MONITOR;
                s.monitor = source->monitor ? source->monitor : "";
                break;
            default: break;
        }
    }
    auto next   = A->state;
    next.source = s;
    A->set(next);
    return true;
}

EXPORT bool hyprture_set_box(uint64_t id, double x, double y, double w, double h) {
    const auto A = monitorAperture(id);
    if (!A)
        return false;
    auto next        = A->state;
    next.destination = {x, y, w, h};
    A->set(next);
    return true;
}

EXPORT bool hyprture_set_style(uint64_t id, float opacity, float radius, bool live) {
    const auto A = monitorAperture(id);
    if (!A)
        return false;
    auto next    = A->state;
    next.opacity = std::clamp(opacity, 0.F, 1.F);
    next.radius  = std::max(0.F, radius);
    next.live    = live;
    A->set(next);
    return true;
}

EXPORT bool hyprture_source_size(uint64_t id, double* w, double* h) {
    const auto A = monitorAperture(id);
    if (!A || A->sourceSize.x <= 0)
        return false;
    if (w)
        *w = A->sourceSize.x;
    if (h)
        *h = A->sourceSize.y;
    return true;
}

EXPORT void hyprture_destroy(uint64_t id) {
    if (monitorAperture(id))
        Aperture::destroy(id);
}
}
