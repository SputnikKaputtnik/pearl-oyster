#include "story/lua_host.h"

#include <cstdio>

#include "story/native_api_names.h"

namespace oyster::story {

namespace {

const char* kHostKey = "oyster.host";
const char* kCacheKey = "oyster.objects";

int traceback(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushstring(L, msg ? msg : "(error object)");
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;
}

// package searcher: module "a/b/c" -> file "a/b/c.lua" resolved case-insensitively
int searcher(lua_State* L) {
    LuaHost& host = LuaHost::from(L);
    std::string name = luaL_checkstring(L, 1);
    for (auto& c : name)
        if (c == '.') c = '/';
    std::string path = host.fs().resolve(name + ".lua");
    if (path.empty()) {
        lua_pushfstring(L, "\n\tno file '%s.lua' in the installation", name.c_str());
        return 1;
    }
    if (luaL_loadfile(L, path.c_str()) != 0) return lua_error(L);
    return 1;
}

int missingStub(lua_State* L) {
    LuaHost& host = LuaHost::from(L);
    const char* name = lua_tostring(L, lua_upvalueindex(1));
    host.missing[name ? name : "?"]++;
    return 0;
}

// __index of every type table's metatable: an original native function this player does not
// implement becomes a logging no-op (cached in the type table); other keys continue with the
// base type (upvalue 1, may be nil) and finally resolve to nil - as in the original, where
// unknown keys of native objects are nil.
int missingIndex(lua_State* L) {
    // (type table, key)
    LuaHost& host = LuaHost::from(L);
    if (lua_type(L, 2) == LUA_TSTRING) {
        lua_getfield(L, 1, "__typename");
        std::string tn = lua_isstring(L, -1) ? lua_tostring(L, -1) : "?";
        lua_pop(L, 1);
        std::string key = lua_tostring(L, 2);
        if (host.isOriginalFunction(tn, key)) {
            lua_pushstring(L, (tn + "." + key).c_str());
            lua_pushcclosure(L, missingStub, 1);
            lua_pushvalue(L, 2);
            lua_pushvalue(L, -2);
            lua_rawset(L, 1);
            return 1;
        }
    }
    if (lua_istable(L, lua_upvalueindex(1))) {
        lua_pushvalue(L, 2);
        lua_gettable(L, lua_upvalueindex(1));
        return 1;
    }
    return 0;
}

}  // namespace

LuaHost::LuaHost(const PackageFS& fs) : fs_(fs) {
    L_ = luaL_newstate();
    luaL_openlibs(L_);
    lua_pushlightuserdata(L_, this);
    lua_setfield(L_, LUA_REGISTRYINDEX, kHostKey);
    // weak object cache
    lua_newtable(L_);
    lua_newtable(L_);
    lua_pushstring(L_, "v");
    lua_setfield(L_, -2, "__mode");
    lua_setmetatable(L_, -2);
    lua_setfield(L_, LUA_REGISTRYINDEX, kCacheKey);
    // searcher for the installation (inserted before the default file searchers)
    lua_getglobal(L_, "package");
    lua_getfield(L_, -1, "loaders");
    int n = static_cast<int>(lua_objlen(L_, -1));
    for (int i = n; i >= 2; --i) {
        lua_rawgeti(L_, -1, i);
        lua_rawseti(L_, -2, i + 1);
    }
    lua_pushcfunction(L_, searcher);
    lua_rawseti(L_, -2, 2);
    lua_pop(L_, 2);
}

LuaHost::~LuaHost() {
    if (L_) lua_close(L_);
}

LuaHost& LuaHost::from(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kHostKey);
    auto* h = static_cast<LuaHost*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return *h;
}

void LuaHost::bindType(const char* name, const luaL_Reg* funcs, const char* base) {
    static const luaL_Reg none[] = {{nullptr, nullptr}};
    luaL_register(L_, name, funcs ? funcs : none);  // global table <name> (extended if it exists)
    lua_pushstring(L_, name);
    lua_setfield(L_, -2, "__typename");
    lua_pop(L_, 1);
    if (luaL_newmetatable(L_, name)) {  // registry metatable for objects of this type
        lua_getglobal(L_, name);
        lua_setfield(L_, -2, "__index");
        lua_pushstring(L_, name);
        lua_setfield(L_, -2, "__name");
    }
    lua_pop(L_, 1);
    if (base) bases_[name] = base;
    bound_.insert(name);
}

bool LuaHost::isOriginalFunction(const std::string& type, const std::string& fn) const {
    auto it = original_.find(type);
    return it != original_.end() && it->second.count(fn) != 0;
}

void LuaHost::finishBindings() {
    // every type of the original exists, implemented or not
    for (const NativeTypeNames* t = kNativeApiNames; t->type; ++t) {
        std::set<std::string>& names = original_[t->type];
        std::string list = t->functions;
        size_t p = 0;
        while (p < list.size()) {
            size_t q = list.find(' ', p);
            if (q == std::string::npos) q = list.size();
            if (q > p) names.insert(list.substr(p, q - p));
            p = q + 1;
        }
        if (!bound_.count(t->type)) bindType(t->type, nullptr);
    }
    for (const auto& t : bound_) {
        lua_getglobal(L_, t.c_str());
        lua_newtable(L_);
        auto b = bases_.find(t);
        if (b != bases_.end()) lua_getglobal(L_, b->second.c_str());
        else lua_pushnil(L_);
        lua_pushcclosure(L_, missingIndex, 1);
        lua_setfield(L_, -2, "__index");
        lua_setmetatable(L_, -2);
        lua_pop(L_, 1);
    }
}

void LuaHost::destroy(uint32_t handle) {
    if (handle == 0 || handle > objects_.size()) return;
    objects_[handle - 1].reset();
    lua_getfield(L_, LUA_REGISTRYINDEX, kCacheKey);
    lua_pushlightuserdata(L_, reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
    lua_pushnil(L_);
    lua_rawset(L_, -3);
    lua_pop(L_, 1);
}

NativeObject* LuaHost::object(uint32_t handle) const {
    if (handle == 0 || handle > objects_.size()) return nullptr;
    return objects_[handle - 1].get();
}

void LuaHost::pushObject(lua_State* L, const NativeObject* o) {
    if (!o) { lua_pushnil(L); return; }
    void* key = reinterpret_cast<void*>(static_cast<uintptr_t>(o->handle));
    lua_getfield(L, LUA_REGISTRYINDEX, kCacheKey);
    lua_pushlightuserdata(L, key);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushlightuserdata(L, key);
    lua_setfield(L, -2, "__object");
    luaL_getmetatable(L, o->typeName.c_str());
    lua_setmetatable(L, -2);
    lua_pushlightuserdata(L, key);
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);
    lua_remove(L, -2);
}

uint32_t LuaHost::toHandle(lua_State* L, int idx) const {
    if (!lua_istable(L, idx)) return 0;
    lua_getfield(L, idx, "__object");
    uint32_t h = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lua_touserdata(L, -1)));
    lua_pop(L, 1);
    return h;
}

namespace {
void pushClassInstance(lua_State* L, const char* cls) {
    lua_newtable(L);
    lua_getglobal(L, cls);
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
    } else {
        lua_pop(L, 1);
    }
}
void setNum(lua_State* L, const char* k, float v) {
    lua_pushnumber(L, static_cast<double>(v));
    lua_setfield(L, -2, k);
}
float getNum(lua_State* L, int idx, const char* k, float def) {
    lua_getfield(L, idx, k);
    float v = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : def;
    lua_pop(L, 1);
    return v;
}
}  // namespace

void LuaHost::pushVec3(lua_State* L, const Vec3& v) {
    pushClassInstance(L, "Vector3");
    setNum(L, "x", v.x);
    setNum(L, "y", v.y);
    setNum(L, "z", v.z);
}

void LuaHost::pushQuat(lua_State* L, const Quat& q) {
    pushClassInstance(L, "Quaternion");
    setNum(L, "x", q.x);
    setNum(L, "y", q.y);
    setNum(L, "z", q.z);
    setNum(L, "w", q.w);
}

void LuaHost::pushSRT(lua_State* L, const Vec3& pos, const Quat& rot, const Vec3& scale) {
    pushClassInstance(L, "SRT");
    lua_pushstring(L, "position");
    pushVec3(L, pos);
    lua_settable(L, -3);
    lua_pushstring(L, "rotation");
    pushQuat(L, rot);
    lua_settable(L, -3);
    lua_pushstring(L, "scale");
    pushVec3(L, scale);
    lua_settable(L, -3);
}

Vec3 LuaHost::toVec3(lua_State* L, int idx, Vec3 def) {
    if (!lua_istable(L, idx)) return def;
    return {getNum(L, idx, "x", def.x), getNum(L, idx, "y", def.y), getNum(L, idx, "z", def.z)};
}

Quat LuaHost::toQuat(lua_State* L, int idx) {
    if (!lua_istable(L, idx)) return Quat();
    return {getNum(L, idx, "x", 0), getNum(L, idx, "y", 0), getNum(L, idx, "z", 0), getNum(L, idx, "w", 1)};
}

bool LuaHost::pcall(int nargs, int nresults) {
    int base = lua_gettop(L_) - nargs;
    lua_pushcfunction(L_, traceback);
    lua_insert(L_, base);
    int rc = lua_pcall(L_, nargs, nresults, base);
    lua_remove(L_, base);
    if (rc != 0) {
        lastError_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "(error)";
        lua_pop(L_, 1);
        std::fprintf(stderr, "lua error: %s\n", lastError_.c_str());
        return false;
    }
    return true;
}

bool LuaHost::doString(const std::string& code, const std::string& chunk) {
    if (luaL_loadbuffer(L_, code.data(), code.size(), chunk.c_str()) != 0) {
        lastError_ = lua_tostring(L_, -1);
        lua_pop(L_, 1);
        std::fprintf(stderr, "lua load error: %s\n", lastError_.c_str());
        return false;
    }
    return pcall(0, 0);
}

bool LuaHost::require(const std::string& module) {
    lua_getglobal(L_, "require");
    lua_pushstring(L_, module.c_str());
    return pcall(1, 0);
}

bool LuaHost::callApplication(const char* fn, const std::vector<double>& args) {
    lua_getglobal(L_, "Application");
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }
    lua_getfield(L_, -1, fn);
    lua_remove(L_, -2);
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return false; }
    for (double a : args) lua_pushnumber(L_, a);
    return pcall(static_cast<int>(args.size()), 0);
}

}  // namespace oyster::story
