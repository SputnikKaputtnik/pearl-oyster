// Lua host for the original story scripts: mirrors the binding model of moxie.v2
// (LUtil::bindType_ / pushNativeType_, see docs/native-api.md).
//
//  * every native type is a global table of functions; its registry metatable has
//    __index = <Type>, __name = "<Type>"; a base type is chained via the type table's metatable
//  * native objects are Lua tables {__object = lightuserdata(handle)} with that metatable, cached
//    per handle in a weak table, so scripts can store their own fields on them
//  * missing native functions resolve to a logging no-op (recorded in `missing`)
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "core/math.h"
#include "core/pkgfs.h"

namespace oyster::story {

// Base of everything handed to Lua as an object.
struct NativeObject {
    virtual ~NativeObject() = default;
    uint32_t handle = 0;
    std::string typeName;  // registry metatable name (= Lua type name)
};

class LuaHost {
public:
    explicit LuaHost(const PackageFS& fs);
    ~LuaHost();
    LuaHost(const LuaHost&) = delete;
    LuaHost& operator=(const LuaHost&) = delete;

    lua_State* L() const { return L_; }
    const PackageFS& fs() const { return fs_; }
    static LuaHost& from(lua_State* L);  // host stored in the registry

    // Types ----------------------------------------------------------------------------
    // Registers (or extends) type <name>; funcs may be null. Call finishBindings() once after all
    // bindings: it creates the remaining original types and installs the inheritance chain plus
    // the fallbacks for original functions that are not implemented.
    void bindType(const char* name, const luaL_Reg* funcs, const char* base = nullptr);
    void finishBindings();
    bool isOriginalFunction(const std::string& type, const std::string& fn) const;

    // Objects --------------------------------------------------------------------------
    template <typename T> std::shared_ptr<T> create(const char* typeName) {
        auto o = std::make_shared<T>();
        o->typeName = typeName;
        o->handle = static_cast<uint32_t>(objects_.size() + 1);
        objects_.push_back(o);
        return o;
    }
    void destroy(uint32_t handle);
    // Stack helpers take the calling thread: natives run inside the story's coroutines, whose
    // stacks differ from the main state's (the overloads without L use the main state).
    void pushObject(lua_State* L, const NativeObject* o);  // pushes nil for nullptr
    void pushObject(const NativeObject* o) { pushObject(L_, o); }
    NativeObject* object(uint32_t handle) const;
    template <typename T> T* toObject(lua_State* L, int idx) const { return dynamic_cast<T*>(object(toHandle(L, idx))); }
    uint32_t toHandle(lua_State* L, int idx) const;  // 0 if not a native object

    // Values ---------------------------------------------------------------------------
    void pushVec3(lua_State* L, const Vec3& v);
    void pushQuat(lua_State* L, const Quat& q);
    void pushSRT(lua_State* L, const Vec3& pos, const Quat& rot, const Vec3& scale);
    static Vec3 toVec3(lua_State* L, int idx, Vec3 def = {});
    static Quat toQuat(lua_State* L, int idx);
    static float toFloat(lua_State* L, int idx, float def = 0) {
        return lua_isnumber(L, idx) ? static_cast<float>(lua_tonumber(L, idx)) : def;
    }

    // Scripts --------------------------------------------------------------------------
    bool doString(const std::string& code, const std::string& chunk = "=host");
    bool require(const std::string& module);
    // Calls Application.<fn>(...) if present; returns false on error (logged).
    bool callApplication(const char* fn, const std::vector<double>& args = {});
    const std::string& lastError() const { return lastError_; }
    // pcall of the function below nargs arguments on the stack, with traceback; logs errors.
    bool pcallTop(int nargs, int nresults) { return pcall(nargs, nresults); }

    std::map<std::string, int> missing;  // "Type.func" -> call count
    std::vector<std::string> log;        // Log.* output
    bool echoLog = false;                // print Log.message too (errors/warnings always)

private:
    bool pcall(int nargs, int nresults);

    const PackageFS& fs_;
    lua_State* L_ = nullptr;
    std::vector<std::shared_ptr<NativeObject>> objects_;
    std::set<std::string> bound_;
    std::map<std::string, std::string> bases_;
    std::map<std::string, std::set<std::string>> original_;
    std::string lastError_;
};

}  // namespace oyster::story
