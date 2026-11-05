#pragma once

// An aperture: a rectangle of a target (a client's surface, or a monitor) in
// which Hyprland draws a source (a workspace, a window, a monitor) live. The
// protocol (Protocol.cpp), the Lua API (Lua.cpp) and the C API (CApi.cpp) all
// create and change them here; Render.cpp draws them.

#include "Fit.hpp"

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class CWLSurfaceResource;
namespace Render {
    class IFramebuffer;
    class CSceneResources;
}

namespace Aperture {
    enum class eScene : uint8_t {
        WINDOWS, // the workspace's windows
        SHELL,   // and its monitor's wallpaper and layer surfaces
    };

    struct SSource {
        enum eKind : uint8_t {
            NONE,
            WORKSPACE,
            WINDOW,
            MONITOR, // its active workspace, with the shell
        } kind = NONE;
        int64_t     workspace = 0;
        std::string window; // address, "0x…"
        std::string monitor;
        eScene      scene = eScene::WINDOWS;

        bool        operator==(const SSource&) const = default;
    };

    struct SState {
        SSource source;
        Fit::SBox destination; // target-local, logical
        float     opacity = 1.F;
        float     radius  = 0.F; // logical
        bool      live    = true;

        bool      operator==(const SState&) const = default;
    };

    // Where an aperture is drawn: a client's surface (Protocol.cpp), or a
    // monitor, at one of two depths (Lua.cpp, CApi.cpp).
    struct STarget {
        WP<CWLSurfaceResource> surface;
        std::string            monitor;
        enum eLevel : uint8_t {
            OVERLAY, // above everything
            WINDOWS, // above windows, below top and overlay layers
        } level = OVERLAY;
    };

    // What a source resolved to this frame.
    struct SResolved {
        PHLWORKSPACE workspace;
        PHLWINDOW    window;
        PHLMONITOR   monitor; // the one the source is laid out on
        eScene       scene = eScene::WINDOWS;
        Vector2D     size; // logical
    };

    class CAperture {
      public:
        explicit CAperture(STarget target);
        ~CAperture();

        const uint64_t id;
        const STarget  target;
        SState         state;

        // The source's logical size changed (zero: gone).
        std::function<void(Vector2D)> onSourceSize;

        void                          set(const SState& next);
        std::optional<SResolved>      resolve() const;
        // The monitor its target is on (nothing: unmapped, or no such monitor).
        PHLMONITOR targetMonitor() const;

        // Where it is drawn now, in global logical coordinates, and the
        // monitor showing it; nothing while hidden.
        struct SPlacement {
            PHLMONITOR monitor;
            CBox       box;  // the fitted view
            CBox       clip; // the target's bounds
            float      alpha = 1.F;
        };
        std::optional<SPlacement> placement() const;

        // Render.cpp's: the view rendered offscreen, redrawn when live or dirty.
        SP<Render::IFramebuffer>    fb;
        SP<Render::CSceneResources> sceneResources;
        bool                        dirty = true;
        Vector2D                    sourceSize;
        std::optional<CBox>         lastDrawn; // global logical, for damage

        void                        damage();
        void                        reportSourceSize(const Vector2D& size);
    };

    SP<CAperture>                     create(STarget target);
    void                              destroy(uint64_t id);
    SP<CAperture>                     find(uint64_t id);
    const std::vector<SP<CAperture>>& all();
    void                              clear();
}
