#include "scene/animation.h"

#include <cmath>
#include <cstring>

namespace oyster {

void AnimationPlayback::setup(std::shared_ptr<const AnimResource> r, int first, int last, bool looping, bool relative) {
    res = std::move(r);
    int base = 0;  // relative frames: subtract the resource's start frame (+0x50; Pearl data: 0)
    (void)relative;
    int lastFrame = static_cast<int>(res->frames) - 1;
    first -= base;
    if (last >= 0 && last >= base) last -= base;
    if (last < 0) last = lastFrame;
    if (first < 0 || static_cast<int>(res->frames) < last - first) {
        if (first < 0) first = 0;
        last = last < lastFrame ? last : lastFrame;
        if (last - first < 0) { first = 0; last = lastFrame; }
    }
    start = static_cast<uint32_t>(first);
    end = static_cast<uint32_t>(last);
    loop = looping;
    speed = 1.0f;
    time = 0;
    fps = res->fps;
    duration = static_cast<float>((last - first) + 1) / static_cast<float>(fps);
    paused = false;
    interpolate = true;
}

void AnimationPlayback::advance(float dt) {
    if (!paused && duration != 0.0f) {
        float step = fixedDt > 0.0f ? fixedDt : dt;
        time = step * speed + time;
    }
    if (duration > 0.0f) {
        if (loop && (maxLoops == 0 || loopsDone + 1 < maxLoops)) {
            int n = 0;
            if (time < 0.0f) {
                do { time += duration; ++n; } while (time < 0.0f);
            }
            if (duration <= time) {
                do { time -= duration; ++n; } while (duration <= time);
            }
            loopsDone += static_cast<uint32_t>(n);
            return;
        }
        if (time < 0.0f) time = 0.0f;
        if (duration <= time) time = duration;
    }
}

float AnimationPlayback::framePosition() const {
    float f = static_cast<float>(fps) * time + static_cast<float>(start);
    if (static_cast<float>(end) <= f) f = static_cast<float>(end);
    return f;
}

KeyPair findKeys(const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache) {
    const uint32_t n = static_cast<uint32_t>(ch.keys.size());
    KeyPair kp;
    uint32_t kf0 = cache.kf0;
    uint32_t kf1 = kf0 + 1;
    if (n - 1 < kf1) kf1 = 0;
    if (n <= kf1) kf1 = 0;
    const float fpos = a.framePosition();
    const auto frameOf = [&](uint32_t k) { return static_cast<float>(ch.keys[k].frame); };

    if (kf0 == 0 || frameOf(kf0) <= fpos) {
        uint32_t cur = kf1;
        if (frameOf(cur) < fpos) {
            uint32_t next;
            do {
                if (n <= cur) break;
                kf0 = cur;
                next = cur + 1;
                kf1 = next < n ? next : 0;
                cur = next;
            } while (frameOf(kf1) <= fpos);
        }
    } else {
        int32_t k = static_cast<int32_t>(kf0);
        uint32_t hi = kf0;
        while (--k >= 0) {
            kf0 = static_cast<uint32_t>(k);
            kf1 = hi < n ? hi : 0;
            if (frameOf(static_cast<uint32_t>(k)) < fpos) break;
            --hi;
        }
    }
    uint32_t f0 = ch.keys[kf0].frame;
    uint32_t d = ch.keys[kf1].frame - f0;  // unsigned, as in the engine
    uint32_t div = 1;
    if (1 < static_cast<int32_t>(d)) div = d;
    float t = (fpos - static_cast<float>(f0)) / static_cast<float>(div);
    if (1.0f <= t) t = 1.0f;
    if (t <= 0.0f) t = 0.0f;
    cache.kf0 = kf0;
    kp.kf0 = kf0;
    kp.kf1 = kf1;
    kp.t = t;
    return kp;
}

namespace {

template <typename T> T valueAt(const AnimTrack& tr, const AnimKey& k) {
    T v;
    std::memcpy(&v, tr.blob.data() + k.offset, sizeof(T));
    return v;
}

// (!interpolate || (!loop && t == 1)) -> pick a key, else interpolate
bool pickKey(const AnimationPlayback& a, float t) { return !a.interpolate || (!a.loop && t == 1.0f); }

}  // namespace

Quat slerpMoxie(float t, const Quat& a, const Quat& b) {
    float d = b.y * a.y + b.x * a.x + a.z * b.z + a.w * b.w;
    Quat o;
    if (static_cast<double>(d) < 0.99) {
        float s = std::sqrt(std::fabs(1.0f - d * d));
        if (0.0099999998f <= s) {
            float sign = d < 0.0f ? -1.0f : 1.0f;
            float ang = std::asin(s);
            float inv = 1.0f / s;
            float ca = std::sin((1.0f - t) * ang) * inv;
            float cb = std::sin(ang * t) * inv * sign;
            o.x = b.x * cb + a.x * ca;
            o.y = cb * b.y + ca * a.y;
            o.z = ca * a.z + cb * b.z;
            o.w = ca * a.w + cb * b.w;
        } else {
            o = a;
        }
        return o;
    }
    // Quaternion::setLerp: component lerp + normalize
    o.x = (b.x - a.x) * t + a.x;
    o.y = (b.y - a.y) * t + a.y;
    o.z = (b.z - a.z) * t + a.z;
    o.w = (b.w - a.w) * t + a.w;
    float l = std::sqrt(o.y * o.y + o.x * o.x + o.z * o.z + o.w * o.w);
    if (0.0f < l) {
        float r = 1.0f / l;
        o.x *= r; o.y *= r; o.z *= r; o.w *= r;
    }
    return o;
}

Vec3 sampleVec3(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache, const SamplingPolicy& p) {
    KeyPair k = findKeys(ch, a, cache);
    const AnimKey& k0 = ch.keys[k.kf0];
    const AnimKey& k1 = ch.keys[k.kf1];
    float v0[3], v1[3];
    std::memcpy(v0, tr.blob.data() + k0.offset, 12);
    std::memcpy(v1, tr.blob.data() + k1.offset, 12);
    if (pickKey(a, k.t)) {
        const float* v = k.t < 1.0f ? v0 : v1;
        return {v[0], v[1], v[2]};
    }
    uint32_t bits = p.interpolateSteppedKeys ? 0 : k0.stepBits;
    float o[3];
    for (int c = 0; c < 3; ++c) o[c] = (bits & (1u << c)) ? v0[c] : (v1[c] - v0[c]) * k.t + v0[c];
    return {o[0], o[1], o[2]};
}

Quat sampleQuat(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache, const SamplingPolicy& p) {
    KeyPair k = findKeys(ch, a, cache);
    const AnimKey& k0 = ch.keys[k.kf0];
    Quat q0 = valueAt<Quat>(tr, k0), q1 = valueAt<Quat>(tr, ch.keys[k.kf1]);
    if (pickKey(a, k.t)) return k.t < 1.0f ? q0 : q1;
    if (k0.stepBits == 0 || p.interpolateSteppedKeys) return slerpMoxie(k.t, q0, q1);
    return q0;
}

float sampleFloat(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache, const SamplingPolicy& p) {
    KeyPair k = findKeys(ch, a, cache);
    const AnimKey& k0 = ch.keys[k.kf0];
    float f0 = valueAt<float>(tr, k0), f1 = valueAt<float>(tr, ch.keys[k.kf1]);
    if (pickKey(a, k.t)) return k.t < 1.0f ? f0 : f1;
    if (k0.stepBits == 0 || p.interpolateSteppedKeys) return (f1 - f0) * k.t + f0;
    return f0;
}

int32_t sampleInt(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache) {
    KeyPair k = findKeys(ch, a, cache);
    const AnimKey& k0 = ch.keys[k.kf0];
    uint32_t i0 = valueAt<uint32_t>(tr, k0), i1 = valueAt<uint32_t>(tr, ch.keys[k.kf1]);
    if (pickKey(a, k.t)) return static_cast<int32_t>(k.t < 1.0f ? i0 : i1);
    float f = k0.stepBits == 0 ? static_cast<float>(static_cast<int32_t>(i1 - i0)) * k.t + static_cast<float>(static_cast<int32_t>(i0))
                               : static_cast<float>(static_cast<int32_t>(i0));
    return f < 0.0f ? static_cast<int32_t>(f - 0.49999997f) : static_cast<int32_t>(f + 0.49999997f);
}

uint32_t sampleBool(const AnimTrack& tr, const AnimChannel& ch, const AnimationPlayback& a, ChannelCache& cache) {
    KeyPair k = findKeys(ch, a, cache);
    uint32_t b0 = valueAt<uint32_t>(tr, ch.keys[k.kf0]), b1 = valueAt<uint32_t>(tr, ch.keys[k.kf1]);
    if (pickKey(a, k.t)) return k.t < 1.0f ? b0 : b1;
    return b0;
}

}  // namespace oyster
