// Helpers shared by the bind_*.cpp files.
#pragma once
#include <string>

#include "story/engine.h"

namespace oyster::story {

// Natives are called both as Type.fn(...) and Type:fn(...); with the colon form the first
// argument is the type table itself (it carries __typename), which is skipped.
inline int firstArg(lua_State* L) {
    if (lua_istable(L, 1)) {
        lua_pushstring(L, "__typename");
        lua_rawget(L, 1);
        bool isType = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (isType) return 2;
    }
    return 1;
}

inline std::string optString(lua_State* L, int idx, const char* def = "") {
    return lua_isstring(L, idx) ? std::string(lua_tostring(L, idx)) : std::string(def);
}
inline double optNumber(lua_State* L, int idx, double def = 0) { return lua_isnumber(L, idx) ? lua_tonumber(L, idx) : def; }
inline bool optBool(lua_State* L, int idx, bool def = false) {
    return lua_isnoneornil(L, idx) ? def : lua_toboolean(L, idx) != 0;
}

template <typename T> T* objectArg(lua_State* L, int idx) { return LuaHost::from(L).toObject<T>(L, idx); }

inline void pushObject(lua_State* L, const NativeObject* o) { LuaHost::from(L).pushObject(L, o); }

// Lua table field helpers for value classes (Vector3 {x,y,z}, Quaternion {x,y,z,w}, Color {r,g,b,a})
inline float fieldOr(lua_State* L, int idx, const char* k, float def) {
    lua_getfield(L, idx, k);
    float v = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : def;
    lua_pop(L, 1);
    return v;
}

}  // namespace oyster::story
