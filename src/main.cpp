// hyprture: apertures, rectangles in which Hyprland draws a workspace, a
// window or a monitor live (Aperture.cpp, Draw.cpp). Clients get them through
// the hyprture_v1 protocol (Protocol.cpp), plugins through the C API
// (hyprture.h, CApi.cpp), configs through Lua (Lua.cpp).

#include "Aperture.hpp"
#include "Draw.hpp"
#include "Lua.hpp"
#include "Protocol.hpp"
#include "../hyprture.h"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/lua/ConfigManager.hpp>

extern "C" {
#include <lua.h>
}

#include <stdexcept>

static HANDLE PHANDLE = nullptr;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Built against a different Hyprland than the one running: the internals we
    // touch may have moved, so refuse to load rather than corrupt state.
    if (std::string{__hyprland_api_get_hash()} != std::string{__hyprland_api_get_client_hash()}) {
        HyprlandAPI::addNotification(PHANDLE, "[hyprture] built for a different Hyprland; not loading", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprture] version mismatch");
    }

    // Built against another Lua than Hyprland's: its constants would point
    // elsewhere.
    if (const auto MGR = Config::Lua::mgr()) {
        const auto LIFE = MGR->luaStateLifetime();
        if (LIFE && LIFE->state && int(lua_version(LIFE->state)) != LUA_VERSION_NUM) {
            HyprlandAPI::addNotification(PHANDLE, "[hyprture] built for another Lua than Hyprland's; not loading", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
            throw std::runtime_error("[hyprture] Lua version mismatch");
        }
    }

    try {
        Draw::init(PHANDLE);
        Lua::init(PHANDLE);
        Protocol::init();
    } catch (...) {
        Protocol::exit();
        Draw::exit();
        throw;
    }

    return {HYPRTURE_PLUGIN_NAME, "Apertures: live views of workspaces and windows, for plugins and clients", "aetherall", "0.1"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Clients' objects first (they own apertures), then the rest.
    Protocol::exit();
    Aperture::clear();
    Draw::exit();
}
