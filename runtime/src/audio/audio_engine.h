// Audio engine: re-implementation of moxie.v2's "Maux" mixer as Pearl uses it
// (decompiled in pearl-work/ghidra/aud*.c, hrtf*.c, lapiaud.c; see docs/audio.md).
//
//  * 48 kHz, stereo int16 output, blocks of 512 frames (AudioSystem::allInitialize defaults).
//  * Players mix decoded int16 PCM with per-block linear volume/pan ramps either straight into the
//    output or into their emitter's buffer, saturating at +-32767 (kernels FUN_180044920 & co).
//  * Emitter type 2D (Lua "2.5D"): Emitter2dEffect - balance pan from the listener's right vector
//    and distance/angle rolloff (FUN_18004ac00 / FUN_18004a380).
//  * Emitter type Surround: AmbisonicFirstOrderStereoEffect - dual-band shelf, rotation by the
//    listener's yaw/pitch, decode to 4 virtual speakers (mid/side), BiquadBinauralFilter per
//    speaker (delay + 2x6 biquads from the .mxhrtf), L = M+S, R = M-S.
#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/math.h"

namespace oyster::audio {

struct PcmClip {
    int channels = 0;
    int rate = 0;
    uint32_t frames = 0;
    std::vector<int16_t> data;  // interleaved
};
// Decodes an Ogg Vorbis file to int16 (throws on error).
std::shared_ptr<PcmClip> decodeVorbis(const std::vector<uint8_t>& file);

class Engine {
public:
    static constexpr int kRate = 48000;
    static constexpr uint32_t kBlock = 512;

    Engine();
    ~Engine();

    bool loadHrtf(const std::vector<uint8_t>& file);  // AudioSystem::setListenerHRTF

    // Emitters (Lua MEMITTERTYPE: 0 = 2.5D, 1 = 3D, 2 = surround); returns id > 0 or 0
    int createEmitter(int luaType);
    void destroyEmitter(int id);
    void setEmitterPosition(int id, const Vec3& p);
    void setEmitterRotation(int id, const Quat& q);
    void setEmitterParam(int id, int index, float value);  // Emitter2dEffect parameter 0..10
    void setListenerPosition(const Vec3& p);
    void setListenerRotation(const Quat& q);

    // Players (one per Sound)
    int createPlayer(std::shared_ptr<const PcmClip> clip);
    void destroyPlayer(int id);
    void play(int id, int emitter);  // setPlayerEmitter + setPlayState(2)
    void stop(int id);               // setPlayState(0): back to frame 0
    void setVolume(int id, float v, uint32_t rampFrames);
    void setPan(int id, float luaPan, uint32_t rampFrames);  // Lua pan -1..1
    float volume(int id);
    bool isPlaying(int id);
    void setLooping(int id, bool loop);
    float timePosition(int id);  // seconds
    void setTimePosition(int id, float seconds);
    void setMasterVolume(float v) { std::lock_guard<std::mutex> g(mutex_); master_ = v; }

    // Renders interleaved stereo int16; any frame count (internally whole blocks).
    void render(int16_t* out, uint32_t frames);

private:
    struct Player;
    struct Emitter;
    struct Speaker;
    void renderBlock(int16_t* out);
    Player* player(int id);
    Emitter* emitter(int id);
    void update2d(Emitter& e);
    void updateRotation(Emitter& e);

    std::mutex mutex_;
    std::vector<std::unique_ptr<Player>> players_;
    std::vector<std::unique_ptr<Emitter>> emitters_;
    std::vector<std::unique_ptr<Speaker>> speakers_;  // 4 when an HRTF is loaded
    std::vector<float> speakerBuf_;                    // 4 x block x (mid, side)
    Vec3 listenerPos_;
    Quat listenerRot_;
    float master_ = 1.0f;
    std::vector<int16_t> pending_;  // rendered but not yet delivered frames
    size_t pendingPos_ = 0;
};

}  // namespace oyster::audio
