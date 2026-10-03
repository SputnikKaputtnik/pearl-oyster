// Core natives: Log, Time, System, FileSystem, Input, Sensor, DisplayDevice, StoryManager /
// StoryFSM, ResourceGroup, Stat, Rpc, Analytics and the global helpers __status / __inspect.
// Platform services the desktop player does not have (Android, analytics, RPC, sensors) answer
// like the original desktop build without such devices.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "story/bind_util.h"

namespace oyster::story {

namespace {

// ---- Log ------------------------------------------------------------------------------------
int logWrite(lua_State* L, const char* level) {
    std::string msg;
    int n = lua_gettop(L);
    for (int i = firstArg(L); i <= n; ++i) {
        if (!msg.empty()) msg += " ";
        msg += lua_isstring(L, i) ? lua_tostring(L, i) : luaL_typename(L, i);
    }
    LuaHost& h = LuaHost::from(L);
    h.log.push_back(std::string(level) + ": " + msg);
    if (h.echoLog || level[0] != 'm') std::printf("[lua %s] %s\n", level, msg.c_str());
    return 0;
}
int Log_fatal(lua_State* L) { return logWrite(L, "fatal"); }
int Log_error(lua_State* L) { return logWrite(L, "error"); }
int Log_warning(lua_State* L) { return logWrite(L, "warning"); }
int Log_message(lua_State* L) { return logWrite(L, "message"); }

// ---- Time (µs clock, values pushed as float seconds like the engine) --------------------------
TimeState& T(lua_State* L) { return engineOf(L).time(); }
float usToSeconds(int64_t us) { return static_cast<float>(us) * 1.0e-6f; }
int Time_dt(lua_State* L) { lua_pushnumber(L, usToSeconds(T(L).dtUs)); return 1; }
int Time_seconds(lua_State* L) { lua_pushnumber(L, usToSeconds(T(L).elapsedUs)); return 1; }
int Time_milliseconds(lua_State* L) { lua_pushnumber(L, static_cast<double>(T(L).elapsedUs / 1000)); return 1; }
int Time_raw(lua_State* L) { lua_pushnumber(L, static_cast<double>(T(L).elapsedUs)); return 1; }
int Time_rawElapsedSeconds(lua_State* L) { lua_pushnumber(L, usToSeconds(T(L).elapsedUs)); return 1; }
int Time_rawElapsedMilliseconds(lua_State* L) { lua_pushnumber(L, static_cast<double>(T(L).elapsedUs / 1000)); return 1; }
int Time_string(lua_State* L) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", usToSeconds(T(L).elapsedUs));
    lua_pushstring(L, buf);
    return 1;
}
int Time_pause(lua_State* L) { T(L).paused = true; return 0; }
int Time_resume(lua_State* L) { T(L).paused = false; return 0; }
int Time_isPaused(lua_State* L) { lua_pushboolean(L, T(L).paused); return 1; }
int Time_step(lua_State* L) { return 0; }
int Time_setTimeScale(lua_State* L) { T(L).scale = static_cast<float>(optNumber(L, firstArg(L), 1)); return 0; }
int Time_getTimeScale(lua_State* L) { lua_pushnumber(L, T(L).scale); return 1; }
int Time_setFixedTimeStep(lua_State* L) { T(L).fixedStepMs = optNumber(L, firstArg(L), 0); return 0; }
int Time_getFixedTimeStep(lua_State* L) { lua_pushnumber(L, T(L).fixedStepMs); return 1; }
int Time_setErrorTime(lua_State* L) { return 0; }
int Time_getErrorTime(lua_State* L) { lua_pushnumber(L, 0); return 1; }
int Time_setErrorCorrectParams(lua_State* L) {
    // app.lua: Time.setErrorCorrectParams(true, 0.25) - stored, only affects real-time pacing
    int a = firstArg(L);
    T(L).errorCorrect = lua_toboolean(L, a) != 0;
    T(L).errorCorrectRate = static_cast<float>(optNumber(L, a + 1, 0));
    return 0;
}
int Time_getErrorCorrectParams(lua_State* L) {
    lua_pushboolean(L, T(L).errorCorrect);
    lua_pushnumber(L, T(L).errorCorrectRate);
    return 2;
}

// ---- System ---------------------------------------------------------------------------------
int System_exit(lua_State* L) { engineOf(L).exitRequested = true; return 0; }
int System_gc(lua_State* L) { lua_gc(L, LUA_GCSTEP, 0); return 0; }
int System_zero(lua_State* L) { lua_pushnumber(L, 0); return 1; }
int System_false(lua_State* L) { lua_pushboolean(L, 0); return 1; }
int System_true(lua_State* L) { lua_pushboolean(L, 1); return 1; }
int System_noop(lua_State* L) { return 0; }
int System_string(lua_State* L) { lua_pushstring(L, "oyster"); return 1; }
int System_createResourceGroup(lua_State* L) {
    Engine& e = engineOf(L);
    auto* g = e.create<ResourceGroupObj>("ResourceGroup");
    g->name = optString(L, firstArg(L));
    pushObject(L, g);
    lua_pushstring(L, g->name.c_str());
    lua_setfield(L, -2, "name");
    return 1;
}
int System_exists(lua_State* L) {
    lua_pushboolean(L, !engineOf(L).fs().resolve(optString(L, firstArg(L))).empty());
    return 1;
}

// ---- ResourceGroup (everything is local: always complete) ------------------------------------
// ResourceGroup:addResource(rtti, uri) / prefetch() / destroy(): background loading (Prefetcher)
int RG_addResource(lua_State* L) {
    auto* g = objectArg<ResourceGroupObj>(L, 1);
    if (!g) return 0;
    std::string rtti = optString(L, 2), uri = optString(L, 3);
    if (uri.empty()) return 0;
    if (rtti == "ModelResource") g->items.emplace_back(Prefetcher::Kind::Model, uri);
    else if (rtti == "AnimResource") g->items.emplace_back(Prefetcher::Kind::Anim, uri);
    else if (rtti == "AudioResource") g->items.emplace_back(Prefetcher::Kind::Audio, uri);
    return 0;
}
int RG_prefetch(lua_State* L) {
    if (auto* g = objectArg<ResourceGroupObj>(L, 1))
        for (const auto& it : g->items) engineOf(L).prefetcher().request(it.first, it.second);
    return 0;
}
int RG_getCount(lua_State* L) {
    auto* g = objectArg<ResourceGroupObj>(L, 1);
    lua_pushnumber(L, g ? static_cast<double>(g->items.size()) : 0.0);
    return 1;
}
int RG_destroy(lua_State* L) {
    if (auto* g = objectArg<ResourceGroupObj>(L, 1)) {
        // results nobody took belong to a branch the story did not go (or were already loaded)
        for (const auto& it : g->items) engineOf(L).prefetcher().drop(it.first, it.second);
        LuaHost::from(L).destroy(g->handle);
    }
    return 0;
}
int RG_status(lua_State* L) {
    lua_newtable(L);
    for (const char* k : {"pending", "coerced", "loaded", "failed"}) {
        lua_newtable(L);
        lua_setfield(L, -2, k);
    }
    return 1;
}

// ---- StoryManager / StoryFSM (local installation: every state is available at once) ----------
int SM_getFSM(lua_State* L) {
    Engine& e = engineOf(L);
    auto* f = e.create<GenericObj>("StoryFSM");
    f->name = optString(L, firstArg(L));
    pushObject(L, f);
    return 1;
}
int SM_sendEvent(lua_State* L) { return 0; }
int SM_true(lua_State* L) { lua_pushboolean(L, 1); return 1; }
int FSM_getName(lua_State* L) {
    auto* f = objectArg<GenericObj>(L, 1);
    lua_pushstring(L, f ? f->name.c_str() : "");
    return 1;
}
int FSM_noop(lua_State* L) { return 0; }

// ---- Input (mouse from the host window; no keyboard input is fed to the story) -------------
int In_false(lua_State* L) { lua_pushboolean(L, 0); return 1; }

// Vector2 the way the original pushes it (FUN_1800dd5f0): table with metatable Vector2
void pushVec2(lua_State* L, float x, float y) {
    lua_createtable(L, 0, 0);
    int t = lua_gettop(L);
    lua_getglobal(L, "Vector2");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, t);
    lua_pushnumber(L, x);
    lua_setfield(L, t, "x");
    lua_pushnumber(L, y);
    lua_setfield(L, t, "y");
}
int In_isMouseMoving(lua_State* L) {
    lua_pushboolean(L, engineOf(L).input().moving);
    return 1;
}
int In_getMouseButton(lua_State* L) {
    // (button [, down-this-frame]): the second form is not tracked (not used by Pearl)
    int a = firstArg(L);
    int b = static_cast<int>(optNumber(L, a, 0));
    bool v = b >= 0 && b < 3 && !(lua_gettop(L) > a && lua_toboolean(L, a + 1)) && engineOf(L).input().buttons[b];
    lua_pushboolean(L, v);
    return 1;
}
int In_getMousePosition(lua_State* L) {
    const InputState& in = engineOf(L).input();
    pushVec2(L, in.mouseX, in.mouseY);
    return 1;
}
int In_getMouseDelta(lua_State* L) {
    const InputState& in = engineOf(L).input();
    pushVec2(L, in.deltaX, in.deltaY);
    return 1;
}
int In_getMouseWheelDelta(lua_State* L) {
    lua_pushnumber(L, engineOf(L).input().wheel);
    return 1;
}
int In_zero(lua_State* L) { lua_pushnumber(L, 0); return 1; }

// ---- DisplayDevice / Sensor ------------------------------------------------------------------
// Desktop mono: DT_HHD without sensors. With an HMD (Engine::hmd) the natives answer like the
// original's OpenVR DisplayDeviceHMD (vtable @0x18024f008): type DT_HMD, orientation = HMD
// rotation, room-scale translation = tracked position (m) x the IPD scalar (FUN_18018d070).
int DD_getType(lua_State* L) {
    // DT_* from common/scripts/displaydevice.lua: DT_HHD 0, DT_HMD 1
    lua_pushnumber(L, engineOf(L).hmd().active ? 1 : 0);
    return 1;
}
int DD_hmdActive(lua_State* L) {
    lua_pushboolean(L, engineOf(L).hmd().active);
    return 1;
}
int DD_getSensorFusionOrientation(lua_State* L) {
    const HmdState& h = engineOf(L).hmd();
    if (!h.active) return 0;
    Quat q = h.orientation;
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 0.0f) q = Quat(q.x / n, q.y / n, q.z / n, q.w / n);
    LuaHost::from(L).pushQuat(L, q);
    return 1;
}
int DD_getSensorFusionTranslation(lua_State* L) {
    Engine& e = engineOf(L);
    if (!e.hmd().active) return 0;
    const Vec3& p = e.hmd().position;
    LuaHost::from(L).pushVec3(L, Vec3(p.x * e.ipdScalar, p.y * e.ipdScalar, p.z * e.ipdScalar));
    return 1;
}
int DD_setInterpupillaryDistanceScalar(lua_State* L) {
    engineOf(L).ipdScalar = static_cast<float>(optNumber(L, firstArg(L), 0));
    return 0;
}
int DD_setFovScalar(lua_State* L) {
    engineOf(L).fovScalar = static_cast<float>(optNumber(L, firstArg(L), 0));
    return 0;
}
int DD_false(lua_State* L) { lua_pushboolean(L, 0); return 1; }
int DD_noop(lua_State* L) { return 0; }
int DD_identityQuat(lua_State* L) { LuaHost::from(L).pushQuat(L, Quat()); return 1; }
int DD_zeroVec(lua_State* L) { LuaHost::from(L).pushVec3(L, Vec3()); return 1; }

// ---- Stat (StatManager disabled: System.isStatManagerEnabled() == false) ---------------------
int Stat_create(lua_State* L) {
    auto* s = engineOf(L).create<GenericObj>("Stat");
    s->name = optString(L, firstArg(L));
    pushObject(L, s);
    return 1;
}
int Stat_noop(lua_State* L) { return 0; }

// ---- globals ---------------------------------------------------------------------------------
int g_status(lua_State* L) {
    // __status(ob): true while the native object behind a script object is alive
    LuaHost& h = LuaHost::from(L);
    uint32_t handle = h.toHandle(L, 1);
    if (handle == 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushboolean(L, h.object(handle) != nullptr);
    return 1;
}
int g_inspect(lua_State* L) {
    lua_pushstring(L, "");
    return 1;
}
int noop(lua_State* L) { return 0; }

// ---- FileSystem (Story:save creates "saves/" next to the working directory) ------------------
// Only relative paths without ".." are created, so a run can never write outside its working
// directory (the player is run from a work folder, never from the installation).
int FS_mkdir(lua_State* L) {
    std::string path = optString(L, firstArg(L));
    std::filesystem::path fp(path);
    bool safe = !path.empty() && fp.is_relative() && !fp.has_root_name();
    for (const auto& part : fp) safe = safe && part != "..";
    std::error_code ec;
    lua_pushboolean(L, safe && (std::filesystem::create_directories(fp, ec) || std::filesystem::is_directory(fp, ec)));
    return 1;
}
int FS_getFilesInDirectory(lua_State* L) {
    lua_newtable(L);
    return 1;
}

}  // namespace

void bindCore(Engine& e) {
    LuaHost& h = e.lua();
    lua_State* L = h.L();

    static const luaL_Reg log[] = {{"fatal", Log_fatal}, {"error", Log_error}, {"warning", Log_warning},
                                   {"message", Log_message}, {nullptr, nullptr}};
    h.bindType("Log", log);

    static const luaL_Reg time[] = {
        {"raw", Time_raw}, {"rawElapsedSeconds", Time_rawElapsedSeconds},
        {"rawElapsedMilliseconds", Time_rawElapsedMilliseconds}, {"dt", Time_dt}, {"seconds", Time_seconds},
        {"milliseconds", Time_milliseconds}, {"string", Time_string}, {"pause", Time_pause}, {"resume", Time_resume},
        {"isPaused", Time_isPaused}, {"step", Time_step}, {"setTimeScale", Time_setTimeScale},
        {"getTimeScale", Time_getTimeScale}, {"timescale", Time_getTimeScale}, {"setFixedTimeStep", Time_setFixedTimeStep},
        {"getFixedTimeStep", Time_getFixedTimeStep}, {"setErrorTime", Time_setErrorTime},
        {"getErrorTime", Time_getErrorTime}, {"setErrorCorrectParams", Time_setErrorCorrectParams},
        {"getErrorCorrectParams", Time_getErrorCorrectParams}, {nullptr, nullptr}};
    h.bindType("Time", time);

    static const luaL_Reg system[] = {
        {"exit", System_exit}, {"gc", System_gc}, {"getPendingResourceCount", System_zero},
        {"flushPendingResources", System_noop}, {"createResourceGroup", System_createResourceGroup},
        {"exists", System_exists}, {"deviceString", System_string}, {"modelString", System_string},
        {"patchNumberString", System_string}, {"buildNumberString", System_string},
        {"majorNumberString", System_string}, {"minorNumberString", System_string}, {"versionString", System_string},
        {"isOSDSupported", System_false}, {"flushPersist", System_noop}, {"isStatManagerEnabled", System_false},
        {"getCPUMemory", System_zero}, {"getMaxCPUMemory", System_zero}, {"setNiceness", System_noop},
        {"reset", System_noop}, {"pauseAllAudio", System_noop}, {"resumeAllAudio", System_noop},
        {nullptr, nullptr}};
    h.bindType("System", system);

    static const luaL_Reg rg[] = {{"addResource", RG_addResource}, {"prefetch", RG_prefetch}, {"getCount", RG_getCount},
                                  {"destroy", RG_destroy}, {"status", RG_status}, {nullptr, nullptr}};
    h.bindType("ResourceGroup", rg);

    static const luaL_Reg sm[] = {{"getFSM", SM_getFSM}, {"sendEvent", SM_sendEvent}, {"canPrefetchState", SM_true},
                                  {nullptr, nullptr}};
    h.bindType("StoryManager", sm);
    static const luaL_Reg fsm[] = {{"getName", FSM_getName}, {"getState", FSM_getName}, {"setState", SM_true},
                                   {"prefetchState", SM_true}, {"downloadState", FSM_noop},
                                   {"canPrefetchState", SM_true}, {"destroy", FSM_noop}, {nullptr, nullptr}};
    h.bindType("StoryFSM", fsm);

    static const luaL_Reg input[] = {
        {"isKeyDown", In_false}, {"isKeyUp", In_false}, {"anyKeyDown", In_false}, {"anyKeyUp", In_false},
        {"isModifierDown", In_false}, {"isModifierUp", In_false}, {"isMouseMoving", In_isMouseMoving},
        {"getMouseButton", In_getMouseButton}, {"getMouseDblClick", In_false},
        {"getMousePosition", In_getMousePosition}, {"getMouseDelta", In_getMouseDelta},
        {"getMouseWheelDelta", In_getMouseWheelDelta}, {"getTouchCount", In_zero},
        {nullptr, nullptr}};
    h.bindType("Input", input);

    static const luaL_Reg dd[] = {
        {"getType", DD_getType}, {"hasSensorRotation", DD_false}, {"getSensorRotation", DD_identityQuat},
        {"hasSensorFusionOrientation", DD_hmdActive}, {"getSensorFusionOrientation", DD_getSensorFusionOrientation},
        {"isRoomBasedVR", DD_hmdActive}, {"hasSensorFusionTranslation", DD_hmdActive},
        {"getSensorFusionTranslation", DD_getSensorFusionTranslation},
        {"setInterpupillaryDistanceScalar", DD_setInterpupillaryDistanceScalar}, {"setFovScalar", DD_setFovScalar},
        {"setSplashPath", DD_noop}, {nullptr, nullptr}};
    h.bindType("DisplayDevice", dd);

    static const luaL_Reg sensor[] = {
        {"isActive", DD_false}, {"setActive", DD_noop}, {"getStatus", In_zero}, {"setSensorFusionEnabled", DD_noop},
        {"getSensorFusionEnabled", DD_false}, {"getSensorFusionOrientation", DD_identityQuat},
        {"setSensorFusionLowpassFilterEnabled", DD_noop}, {"setSensorFusionLowpassFilterEnable", DD_noop},
        {"setSensorFusionLowpassFilterCurve", DD_noop}, {"pause", DD_noop}, {"resume", DD_noop}, {nullptr, nullptr}};
    h.bindType("Sensor", sensor);

    static const luaL_Reg fs[] = {{"mkdir", FS_mkdir}, {"getFilesInDirectory", FS_getFilesInDirectory}, {nullptr, nullptr}};
    h.bindType("FileSystem", fs);

    static const luaL_Reg stat[] = {{"create", Stat_create}, {"destroy", Stat_noop}, {"set", Stat_noop},
                                    {"get", Stat_noop}, {"find", Stat_noop}, {nullptr, nullptr}};
    h.bindType("Stat", stat);

    // Analytics: registered by the engine under its own module (luaL_Reg @0x180309e00); all no-ops
    static const luaL_Reg analytics[] = {
        {"setVersionString", noop}, {"getDefaultUID", g_inspect}, {"initialize", noop}, {"dispatch", noop},
        {"sendSessionComplete", noop}, {"setEnabled", noop}, {"setAnonymizeMode", noop},
        {"setCustomDimension", noop}, {"setCustomMetric", noop}, {"sendEvent", noop}, {"sendView", noop},
        {"markCrash", noop}, {"start", noop}, {"stop", noop}, {"setState", noop}, {"setFsmState", noop},
        {nullptr, nullptr}};
    h.bindType("Analytics", analytics);

    lua_register(L, "__status", g_status);
    lua_register(L, "__inspect", g_inspect);
    lua_newtable(L);
    lua_setglobal(L, "Persist");
}

}  // namespace oyster::story
