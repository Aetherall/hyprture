// The Lua API, for apertures on a monitor (a client draws its own through the
// protocol instead):
//
//   local id = hl.plugin.hyprture.aperture({
//       monitor = "DP-3",                    -- where it is drawn
//       level   = "overlay",                 -- or "windows": below top/overlay layers
//       box     = { x = 40, y = 40, w = 480, h = 270 }, -- monitor-local, logical
//       source  = { workspace = 6 },         -- or { workspace = 6, scene = "shell" },
//                                            -- { window = "0x…" }, { monitor = "DP-2" }
//       opacity = 1, radius = 12, live = true,
//   })
//   hl.plugin.hyprture.update(id, { box = …, source = …, … }) -- any of the above but monitor/level
//   hl.plugin.hyprture.remove(id)
//   hl.plugin.hyprture.source_size(id) -> w, h  (logical; 0, 0 while the source doesn't exist)

#include "Lua.hpp"
#include "Aperture.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <stdexcept>
#include <string>

namespace {
    std::optional<double> number(lua_State* L, int table, const char* key) {
        lua_getfield(L, table, key);
        std::optional<double> v;
        if (lua_isnumber(L, -1))
            v = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return v;
    }

    std::optional<std::string> string(lua_State* L, int table, const char* key) {
        lua_getfield(L, table, key);
        std::optional<std::string> v;
        if (lua_type(L, -1) == LUA_TSTRING)
            v = lua_tostring(L, -1);
        lua_pop(L, 1);
        return v;
    }

    Aperture::SSource source(lua_State* L, int table) {
        Aperture::SSource s;
        if (lua_type(L, table) != LUA_TTABLE)
            return s; // false / nil: nothing
        if (const auto WS = number(L, table, "workspace")) {
            s.kind      = Aperture::SSource::WORKSPACE;
            s.workspace = sc<int64_t>(*WS);
            if (string(L, table, "scene").value_or("windows") == "shell")
                s.scene = Aperture::eScene::SHELL;
        } else if (const auto W = string(L, table, "window")) {
            s.kind   = Aperture::SSource::WINDOW;
            s.window = *W;
        } else if (const auto M = string(L, table, "monitor")) {
            s.kind    = Aperture::SSource::MONITOR;
            s.monitor = *M;
        } else
            luaL_error(L, "hyprture: source needs workspace, window or monitor");
        return s;
    }

    // The fields of the options table at `table` over `state`.
    Aperture::SState apply(lua_State* L, int table, Aperture::SState state) {
        lua_getfield(L, table, "box");
        if (lua_type(L, -1) == LUA_TTABLE) {
            const int BOX     = lua_gettop(L);
            state.destination = {number(L, BOX, "x").value_or(0), number(L, BOX, "y").value_or(0), number(L, BOX, "w").value_or(0), number(L, BOX, "h").value_or(0)};
        }
        lua_pop(L, 1);

        lua_getfield(L, table, "source");
        if (!lua_isnil(L, -1))
            state.source = source(L, lua_gettop(L));
        lua_pop(L, 1);

        if (const auto V = number(L, table, "opacity"))
            state.opacity = std::clamp(sc<float>(*V), 0.F, 1.F);
        if (const auto V = number(L, table, "radius"))
            state.radius = std::max(0.F, sc<float>(*V));
        lua_getfield(L, table, "live");
        if (lua_isboolean(L, -1))
            state.live = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return state;
    }

    SP<Aperture::CAperture> apertureArg(lua_State* L) {
        const auto A = Aperture::find(sc<uint64_t>(luaL_checkinteger(L, 1)));
        if (!A || A->target.surface)
            luaL_error(L, "hyprture: no such aperture");
        return A;
    }

    int luaAperture(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        Aperture::STarget target;
        target.monitor = string(L, 1, "monitor").value_or("");
        if (target.monitor.empty())
            return luaL_error(L, "hyprture.aperture: monitor is required");
        const auto LEVEL = string(L, 1, "level").value_or("overlay");
        if (LEVEL != "overlay" && LEVEL != "windows")
            return luaL_error(L, "hyprture.aperture: level is \"overlay\" or \"windows\"");
        target.level = LEVEL == "windows" ? Aperture::STarget::WINDOWS : Aperture::STarget::OVERLAY;

        const auto A = Aperture::create(std::move(target));
        A->set(apply(L, 1, A->state));
        lua_pushinteger(L, sc<lua_Integer>(A->id));
        return 1;
    }

    int luaUpdate(lua_State* L) {
        const auto A = apertureArg(L);
        luaL_checktype(L, 2, LUA_TTABLE);
        A->set(apply(L, 2, A->state));
        return 0;
    }

    int luaRemove(lua_State* L) {
        Aperture::destroy(apertureArg(L)->id);
        return 0;
    }

    int luaSourceSize(lua_State* L) {
        const auto A = apertureArg(L);
        lua_pushnumber(L, A->sourceSize.x);
        lua_pushnumber(L, A->sourceSize.y);
        return 2;
    }
}

void Lua::init(void* handle) {
    const bool OK = HyprlandAPI::addLuaFunction(handle, "hyprture", "aperture", luaAperture) && HyprlandAPI::addLuaFunction(handle, "hyprture", "update", luaUpdate) &&
        HyprlandAPI::addLuaFunction(handle, "hyprture", "remove", luaRemove) && HyprlandAPI::addLuaFunction(handle, "hyprture", "source_size", luaSourceSize);
    if (!OK)
        throw std::runtime_error("[hyprture] failed to register its Lua functions");
}
