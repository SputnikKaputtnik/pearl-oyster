// AudioManager natives (LApiAudioManager, decompiled in pearl-work/ghidra/lapiaud.c) on top of
// audio::Engine. Sounds and emitters are native handles the scripts only pass around.
#include <cstdio>

#include "audio/audio_engine.h"
#include "story/bind_util.h"

namespace oyster::story {

namespace {

audio::Engine& A(lua_State* L) { return engineOf(L).audio(); }

int idOf(lua_State* L, int idx) {
    auto* g = objectArg<GenericObj>(L, idx);
    return g ? static_cast<int>(g->numbers["id"]) : 0;
}

Vec3 vecArg(lua_State* L, int idx) {
    if (!lua_istable(L, idx)) return {};
    return {fieldOr(L, idx, "x", 0), fieldOr(L, idx, "y", 0), fieldOr(L, idx, "z", 0)};
}
Quat quatArg(lua_State* L, int idx) {
    if (!lua_istable(L, idx)) return Quat();
    return {fieldOr(L, idx, "x", 0), fieldOr(L, idx, "y", 0), fieldOr(L, idx, "z", 0), fieldOr(L, idx, "w", 1)};
}

// ms -> frames as AudioSystem::setPlayer*Timed does: (uint)(seconds * sample rate)
uint32_t msToFrames(double ms) {
    float s = static_cast<float>(static_cast<int64_t>(ms)) * 0.001f;
    return static_cast<uint32_t>(static_cast<int64_t>(s * static_cast<float>(audio::Engine::kRate)));
}

int AM_addSound(lua_State* L) {
    // (uri, startMs, stopMs, precache) -> Sound
    Engine& e = engineOf(L);
    int a = firstArg(L);
    std::string uri = optString(L, a);
    std::shared_ptr<const audio::PcmClip> clip = e.audioClip(uri);
    if (!clip) return 0;
    auto* s = e.create<GenericObj>("Sound");
    s->name = uri;
    s->numbers["id"] = A(L).createPlayer(clip);
    pushObject(L, s);
    return 1;
}
int AM_removeSound(lua_State* L) {
    if (auto* g = objectArg<GenericObj>(L, firstArg(L))) {
        A(L).destroyPlayer(static_cast<int>(g->numbers["id"]));
        LuaHost::from(L).destroy(g->handle);
    }
    return 0;
}
int AM_playSound(lua_State* L) {
    // (sound, emitter, master, intervalScalar) - quantised start (state 3) is unused by Pearl
    int a = firstArg(L);
    A(L).play(idOf(L, a), idOf(L, a + 1));
    return 0;
}
int AM_stopSound(lua_State* L) {
    A(L).stop(idOf(L, firstArg(L)));
    return 0;
}
int AM_setSoundVolume(lua_State* L) {
    int a = firstArg(L);
    A(L).setVolume(idOf(L, a), static_cast<float>(optNumber(L, a + 1, 1)), msToFrames(optNumber(L, a + 2, 0)));
    return 0;
}
int AM_setSoundPan(lua_State* L) {
    int a = firstArg(L);
    A(L).setPan(idOf(L, a), static_cast<float>(optNumber(L, a + 1, 0)), msToFrames(optNumber(L, a + 2, 0)));
    return 0;
}
int AM_getSoundVolume(lua_State* L) {
    lua_pushnumber(L, A(L).volume(idOf(L, firstArg(L))));
    return 1;
}
int AM_getSoundPan(lua_State* L) {
    // AudioPlayerMaux::getPanBalance is the shared "return 0" stub in the original
    lua_pushnumber(L, 0);
    return 1;
}
int AM_getSoundIsPlaying(lua_State* L) {
    lua_pushboolean(L, A(L).isPlaying(idOf(L, firstArg(L))));
    return 1;
}
int AM_setSoundLoops(lua_State* L) {
    int a = firstArg(L);
    A(L).setLooping(idOf(L, a), lua_toboolean(L, a + 1) != 0);
    return 0;
}
int AM_getSoundTimePosition(lua_State* L) {
    lua_pushnumber(L, A(L).timePosition(idOf(L, firstArg(L))));
    return 1;
}
int AM_setSoundTimePosition(lua_State* L) {
    int a = firstArg(L);
    A(L).setTimePosition(idOf(L, a), static_cast<float>(optNumber(L, a + 1, 0)));
    return 0;
}
int AM_addEmitter(lua_State* L) {
    // (type, enabled) with Lua MEMITTERTYPE 0 = 2.5D, 1 = 3D, 2 = surround
    Engine& e = engineOf(L);
    int a = firstArg(L);
    int type = static_cast<int>(optNumber(L, a, 0));
    int id = A(L).createEmitter(type);
    if (!id) return 0;
    auto* g = e.create<GenericObj>("AudioEmitter");
    g->numbers["id"] = id;
    pushObject(L, g);
    return 1;
}
int AM_destroyEmitter(lua_State* L) {
    if (auto* g = objectArg<GenericObj>(L, firstArg(L))) {
        A(L).destroyEmitter(static_cast<int>(g->numbers["id"]));
        LuaHost::from(L).destroy(g->handle);
    }
    return 0;
}
int AM_setEmitterPosition(lua_State* L) {
    int a = firstArg(L);
    A(L).setEmitterPosition(idOf(L, a), vecArg(L, a + 1));
    return 0;
}
int AM_setEmitterRotation(lua_State* L) {
    int a = firstArg(L);
    A(L).setEmitterRotation(idOf(L, a), quatArg(L, a + 1));
    return 0;
}
int AM_setEmitterEffectParameter(lua_State* L) {
    // (emitter, effect index, parameter, value): parameter - 8 = Emitter2dEffect index
    int a = firstArg(L);
    A(L).setEmitterParam(idOf(L, a), static_cast<int>(optNumber(L, a + 2, 0)) - 8, static_cast<float>(optNumber(L, a + 3, 0)));
    return 0;
}
int AM_setListenerPosition(lua_State* L) {
    A(L).setListenerPosition(vecArg(L, firstArg(L)));
    return 0;
}
int AM_setListenerRotation(lua_State* L) {
    A(L).setListenerRotation(quatArg(L, firstArg(L)));
    return 0;
}
int AM_setHRTFUri(lua_State* L) {
    engineOf(L).loadHrtf(optString(L, firstArg(L)));
    return 0;
}
int AM_setMasterVolume(lua_State* L) {
    A(L).setMasterVolume(static_cast<float>(optNumber(L, firstArg(L), 1)));
    return 0;
}
int noop(lua_State* L) { return 0; }

}  // namespace

void bindAudio(Engine& e) {
    static const luaL_Reg am[] = {
        {"pause", noop}, {"resume", noop}, {"setHRTFUri", AM_setHRTFUri}, {"setHRTF", AM_setHRTFUri},
        {"setListenerPosition", AM_setListenerPosition}, {"setListenerRotation", AM_setListenerRotation},
        {"setListenerVelocity", noop}, {"setPlayerResetMode", noop}, {"setResourceResidentThreshold", noop},
        {"setMasterVolume", AM_setMasterVolume}, {"addEmitter", AM_addEmitter}, {"setEmitterTag", noop},
        {"destroyEmitter", AM_destroyEmitter}, {"setEmitterPosition", AM_setEmitterPosition},
        {"setEmitterRotation", AM_setEmitterRotation}, {"setEmitterVelocity", noop},
        {"setEmitterEffectParameter", AM_setEmitterEffectParameter}, {"setEffectParameter", noop},
        {"addSound", AM_addSound}, {"setSoundTag", noop}, {"removeSound", AM_removeSound}, {"playSound", AM_playSound},
        {"stopSound", AM_stopSound}, {"setSoundVolume", AM_setSoundVolume}, {"setSoundPan", AM_setSoundPan},
        {"setSoundTimePosition", AM_setSoundTimePosition}, {"getSoundTimePosition", AM_getSoundTimePosition},
        {"getSoundIsPlaying", AM_getSoundIsPlaying}, {"setSoundLoops", AM_setSoundLoops},
        {"getSoundVolume", AM_getSoundVolume}, {"getSoundPan", AM_getSoundPan}, {"setSoundQuantization", noop},
        {"prerollSound", noop}, {nullptr, nullptr}};
    e.lua().bindType("AudioManager", am);
    e.lua().bindType("Sound", nullptr);
    e.lua().bindType("AudioEmitter", nullptr);
}

}  // namespace oyster::story
