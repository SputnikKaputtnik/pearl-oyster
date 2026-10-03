#include <cstdio>

#include "story/engine.h"

namespace oyster::story {

// Lua definition tables (render graphs, render views) -> Json for the C++ builders. Arrays are
// tables whose keys are exactly 1..n; class instances (Color, Vector3) become objects with their
// fields; functions and userdata are dropped.
Json luaToJson(lua_State* L, int idx, int depth) {
    if (idx < 0) idx = lua_gettop(L) + idx + 1;
    switch (lua_type(L, idx)) {
    case LUA_TNUMBER: return Json::makeNumber(lua_tonumber(L, idx));
    case LUA_TBOOLEAN: return Json::makeBool(lua_toboolean(L, idx) != 0);
    case LUA_TSTRING: return Json::makeString(lua_tostring(L, idx));
    case LUA_TTABLE: break;
    default: return Json();
    }
    if (depth > 32) return Json();
    size_t n = lua_objlen(L, idx);
    size_t count = 0;
    bool arrayLike = true;
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        ++count;
        if (lua_type(L, -2) != LUA_TNUMBER) arrayLike = false;
        lua_pop(L, 1);
    }
    if (arrayLike && count == n && n > 0) {
        Json a = Json::makeArray();
        for (size_t i = 1; i <= n; ++i) {
            lua_rawgeti(L, idx, static_cast<int>(i));
            a.push(luaToJson(L, -1, depth + 1));
            lua_pop(L, 1);
        }
        return a;
    }
    Json o = Json::makeObject();
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        std::string key;
        if (lua_type(L, -2) == LUA_TSTRING) {
            key = lua_tostring(L, -2);
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", lua_tonumber(L, -2));
            key = buf;
        }
        if (!key.empty()) o.set(key, luaToJson(L, -1, depth + 1));
        lua_pop(L, 1);
    }
    return o;
}

}  // namespace oyster::story
