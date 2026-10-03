// SGParticleEmitter simulation (moxie.v2, decompiled in pearl-work/ghidra/sgpe.c, part*.c,
// noise.c; semantics in docs/particles.md). Particles live in world space for Pearl's systems
// (definition +0x1e9 = 1) and are drawn as camera-facing quads by Renderer.
#pragma once
#include <memory>
#include <vector>

#include "assets/particles.h"
#include "core/math.h"

namespace oyster {

// The CRT rand() of the original (UCRT LCG, default seed 1 - moxie never calls srand). All
// emitters draw from one sequence, as they did on the original's main thread.
struct CrtRand {
    uint32_t state = 1;
    int next() {
        state = state * 214013u + 2531011u;
        return static_cast<int>((state >> 16) & 0x7FFF);
    }
};
CrtRand& particleRand();

class ParticleEmitter {
public:
    explicit ParticleEmitter(std::shared_ptr<const ParticleSystemResource> res);

    // SGParticleEmitter::internalUpdate(frame, dt): dt in seconds (global dt x time scale);
    // `world` = the emitter's world transform (position, rotation quaternion, scale).
    void update(float dt, const Vec3& worldPos, const Quat& worldRot, const Mat4& worldMatrix);
    void reset();

    const ParticleSystemResource& resource() const { return *res_; }
    const std::vector<Particle>& particles() const { return p_; }
    bool boundsValid() const { return boundsValid_; }
    Vec3 boundsMin() const { return lo_; }
    Vec3 boundsMax() const { return hi_; }

    // script-facing state (ParticleEmitter natives)
    float timeScale = 1.0f;      // +0x17c
    bool customRate = false;     // +0x184
    float rate = 0;              // +0x180
    float color[4] = {1, 1, 1, 1};  // +0x190 (u_color)
    bool enabled = true;

private:
    void emit(uint32_t n, const Quat& rot, const Mat4& m);
    void integrate(float dt);

    std::shared_ptr<const ParticleSystemResource> res_;
    std::vector<Particle> p_;
    float age_ = 0;     // +0x178
    float accum_ = 0;   // +0x188
    uint32_t emitted_ = 0;  // +0x18c
    bool warmup_;       // +0x1a0 (definition +0x1c)
    bool boundsValid_ = false;
    Vec3 lo_, hi_;
};

}  // namespace oyster
