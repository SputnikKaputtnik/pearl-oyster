// Animation playback and channel sampling with the semantics of the original engine
// (Animation time update FUN_1800a3dd0, key search FUN_1800a3b60, samplers FUN_1800a55e0 /
// FUN_1800a5970 / FUN_1800a5c50 / FUN_1800a5ec0 / FUN_1800a6180, Quaternion::setSlerp/setLerp).
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "assets/anim.h"
#include "core/math.h"

namespace oyster {

// How animation data between authored 30 fps frames is evaluated.
struct SamplingPolicy {
    // Original: per-key "stepped" bits from Maya are honoured (camera translation is mostly
    // stepped, character vertex animation is stepped per mesh mode). Remaster options relax this.
    bool interpolateSteppedKeys = false;    // transform/material keys flagged as stepped
    bool interpolateSteppedVertexAnim = false;  // VANM meshes with the stepped mode bit
    static SamplingPolicy original() { return {}; }
    static SamplingPolicy remaster() { return {true, true}; }
};

// One playing clip: Animator::addAnimation(name, uri, first, last, loop, relativeFrames).
struct AnimationPlayback {
    std::shared_ptr<const AnimResource> res;
    uint32_t start = 0, end = 0;  // frame range (inclusive)
    uint32_t fps = 30;            // integer header fps (+0x94)
    float duration = 0;           // (end - start + 1) / fps (+0x98)
    bool loop = false;            // +0x9c
    bool interpolate = true;      // +0xe5 (constructor default 1; Pearl never changes it)
    bool paused = false;          // +0xe4
    float speed = 1.0f;           // +0xac
    float time = 0;               // +0xc0
    uint32_t maxLoops = 0, loopsDone = 0;  // +0xa4 / +0xa0
    float fixedDt = 0;            // +0xe0 (<= 0: use the frame dt)

    void setup(std::shared_ptr<const AnimResource> r, int first, int last, bool looping, bool relative);
    void advance(float dt);       // FUN_1800a3dd0
    bool playing() const { return time < duration; }  // +0xc4
    float framePosition() const;  // fps * time + start, clamped to end
};

// Per-channel key cache (anim->m_frameCache): the engine continues the key search from the
// previous result, which matters for exact behaviour at key boundaries.
struct ChannelCache {
    uint32_t kf0 = 0;
};

struct KeyPair {
    uint32_t kf0 = 0, kf1 = 0;
    float t = 0;
};
KeyPair findKeys(const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache);

Vec3 sampleVec3(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache, const SamplingPolicy& p);
Quat sampleQuat(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache, const SamplingPolicy& p);
float sampleFloat(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache, const SamplingPolicy& p);
int32_t sampleInt(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache);
uint32_t sampleBool(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache);

Quat slerpMoxie(float t, const Quat& a, const Quat& b);  // Quaternion::setSlerp

constexpr uint32_t kAttrTranslation = 0xcbd2d62c;  // fnv1a("translation")
constexpr uint32_t kAttrRotation = 0x21ac415f;     // fnv1a("rotation")
constexpr uint32_t kAttrScale = 0x82971c71;        // fnv1a("scale")

}  // namespace oyster
