#include "scene/particles.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace oyster {

namespace {

constexpr float kRandScale = 3.0518509e-05f;  // 1 / 32767 (DAT_180252ccc)
constexpr float kDead = 3.4028235e+38f;       // FLT_MAX marks a dead particle

float frand(CrtRand& r) { return static_cast<float>(r.next()) * kRandScale; }

// MOXIE::FastNoise: value noise over Ken Perlin's permutation table (gradient = p/255*2-1)
// with smoothstep (3 - 2t) t^2 interpolation.
struct FastNoise {
    int perm[512];
    float grad[512];
    FastNoise() {
        static const uint8_t p[256] = {
            151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194, 233, 7,   225, 140, 36,  103, 30,  69,  142,
            8,   99,  37,  240, 21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,  197, 62,  94,  252, 219, 203,
            117, 35,  11,  32,  57,  177, 33,  88,  237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,  165,
            71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111, 229, 122, 60,  211, 133, 230, 220, 105, 92,  41,
            55,  46,  245, 40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,  209, 76,  132, 187, 208, 89,
            18,  169, 200, 196, 135, 130, 116, 188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217, 226, 250,
            124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,  212, 207, 206, 59,  227, 47,  16,  58,  17,  182, 189,
            28,  42,  223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221, 153, 101, 155, 167, 43,  172, 9,
            129, 22,  39,  253, 19,  98,  108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,  228, 251, 34,
            242, 193, 238, 210, 144, 12,  191, 179, 162, 241, 81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,
            181, 199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,   150, 254, 138, 236, 205, 93,  222, 114,
            67,  29,  24,  72,  243, 141, 128, 195, 78,  66,  215, 61,  156, 180};
        for (int i = 0; i < 256; ++i) perm[i] = perm[i + 256] = p[i];
        const uint32_t inv255Bits = 0x3B808081u;  // 1/255 as stored in the engine (@0x1802538c0)
        float inv255;
        std::memcpy(&inv255, &inv255Bits, 4);
        for (int i = 0; i < 512; ++i) grad[i] = static_cast<float>(perm[i]) * inv255 * 2.0f - 1.0f;
    }
    float noise(float x, float y, float z) const {
        float fx = static_cast<float>(static_cast<int>(x));
        if (x <= 0.0f) fx = fx - 1.0f;
        float fy = static_cast<float>(static_cast<int>(y));
        if (y <= 0.0f) fy = fy - 1.0f;
        float fz = static_cast<float>(static_cast<int>(z));
        if (z <= 0.0f) fz = fz - 1.0f;
        int Y = static_cast<int>(fy) & 0xFF, Z = static_cast<int>(fz) & 0xFF, X = static_cast<int>(fx) & 0xFF;
        float tx = x - static_cast<float>(static_cast<int>(fx));
        int a = perm[X] + Y;
        int b = Y + perm[X + 1];
        int aa = perm[a] + Z;
        float tz = z - static_cast<float>(static_cast<int>(fz));
        float ty = y - static_cast<float>(static_cast<int>(fy));
        int ba = perm[b] + Z;
        int ab = perm[a + 1] + Z;
        int bb = perm[b + 1] + Z;
        float sx = (3.0f - (tx + tx)) * tx * tx;
        float g = grad[aa];
        float sy = (3.0f - (ty + ty)) * ty * ty;
        float gab = grad[ab];
        float x1 = (grad[ba] - g) * sx + g;
        float g1 = grad[aa + 1];
        float gab1 = grad[ab + 1];
        float x1b = (grad[ba + 1] - g1) * sx + g1;
        float y1 = (((grad[bb] - gab) * sx + gab) - x1) * sy + x1;
        return (3.0f - (tz + tz)) * tz * tz * (((((grad[bb + 1] - gab1) * sx + gab1) - x1b) * sy + x1b) - y1) + y1;
    }
};
const FastNoise& fastNoise() {
    static const FastNoise n;
    return n;
}

// FUN_18011ea30: uniformly distributed unit vector (rejection sampling in the unit cube)
void randomUnit(CrtRand& r, float out[3]) {
    float x, y, z, l;
    do {
        int i = r.next();
        x = (static_cast<float>(i) * kRandScale + static_cast<float>(i) * kRandScale) - 1.0f;
        i = r.next();
        y = (static_cast<float>(i) * kRandScale + static_cast<float>(i) * kRandScale) - 1.0f;
        i = r.next();
        z = (static_cast<float>(i) * kRandScale + static_cast<float>(i) * kRandScale) - 1.0f;
        l = y * y + x * x + z * z;
    } while (1.0f < l);
    float s = 1.0f / std::sqrt(l);
    out[2] = s * z;
    out[0] = s * x;
    out[1] = s * y;
}

// 2 * (random in [0,1]) - 1 with the original's operand order
float signedRand(CrtRand& r) {
    int i = r.next();
    return (static_cast<float>(i) * kRandScale + static_cast<float>(i) * kRandScale) - 1.0f;
}

}  // namespace

CrtRand& particleRand() {
    static CrtRand r;
    return r;
}

ParticleEmitter::ParticleEmitter(std::shared_ptr<const ParticleSystemResource> res) : res_(std::move(res)) {
    warmup_ = res_->b(0x1c) != 0;
    p_.reserve(1024);
}

void ParticleEmitter::reset() {
    age_ = 0;
    p_.clear();
    accum_ = 0;
}

// FUN_18011ec40: initialise n new particles (CRT rand() call order preserved)
void ParticleEmitter::emit(uint32_t n, const Quat& q, const Mat4& M) {
    const ParticleSystemResource& d = *res_;
    CrtRand& R = particleRand();
    for (uint32_t k = 0; k < n; ++k) {
        Particle P{};
        int i;
        i = R.next();
        P.age = static_cast<float>(i) * kRandScale * (d.f(0x144) - d.f(0x140)) + d.f(0x140);
        i = R.next();
        P.rot = static_cast<float>(i) * kRandScale * (d.f(0x17c) - d.f(0x178)) + d.f(0x178);
        i = R.next();
        float t = static_cast<float>(i) * kRandScale;
        for (int c = 0; c < 4; ++c) {
            float a = d.f(0x148 + 4 * c), b = d.f(0x158 + 4 * c);
            P.col[c] = static_cast<uint8_t>(static_cast<int64_t>(((b - a) * t + a) * 255.0f));
        }
        i = R.next();
        P.size[0] = (d.f(0x170) - d.f(0x168)) * static_cast<float>(i) * kRandScale + d.f(0x168);
        P.size[1] = (d.f(0x174) - d.f(0x16c)) * static_cast<float>(i) * kRandScale + d.f(0x16c);
        i = R.next();
        P.life = static_cast<float>(i) * kRandScale * (d.f(0x4) - d.f(0x0)) + d.f(0x0);
        i = R.next();
        P.frame = static_cast<float>(i) * kRandScale *
                      (static_cast<float>(d.u(0x1e4)) - static_cast<float>(d.u(0x1e0))) +
                  static_cast<float>(d.u(0x1e0));
        i = R.next();
        P.invMass = 1.0f / (static_cast<float>(i) * kRandScale * (d.f(0x1a4) - d.f(0x1a0)) + d.f(0x1a0));
        i = R.next();
        P.drag = -d.f(0x1a8) - static_cast<float>(i) * kRandScale * (d.f(0x1ac) - d.f(0x1a8));
        i = R.next();
        P.rnd = static_cast<float>(i) * kRandScale;
        i = R.next();
        float speed = static_cast<float>(i) * kRandScale * (d.f(0x19c) - d.f(0x198)) + d.f(0x198);

        // emission direction (+0x200): 0 random, 1 outward from the emission point, 2 box
        float dir[3] = {0, 0, 0};
        uint32_t dirMode = d.u(0x200);
        if (dirMode == 0) {
            randomUnit(R, dir);
        } else if (dirMode == 2) {
            i = R.next();
            dir[0] = static_cast<float>(i) * kRandScale * (d.f(0x18c) - d.f(0x180)) + d.f(0x180);
            i = R.next();
            dir[1] = static_cast<float>(i) * kRandScale * (d.f(0x190) - d.f(0x184)) + d.f(0x184);
            i = R.next();
            dir[2] = static_cast<float>(i) * kRandScale * (d.f(0x194) - d.f(0x188)) + d.f(0x188);
            float l = std::sqrt(dir[1] * dir[1] + dir[0] * dir[0] + dir[2] * dir[2]);
            if (0.0f < l) {
                l = 1.0f / l;
                dir[0] *= l;
                dir[2] *= l;
                dir[1] *= l;
            }
        }
        auto outward = [&](float x, float y, float z) {
            float l = std::sqrt(y * y + x * x + z * z);
            if (0.0f < l) {
                l = 1.0f / l;
                dir[1] = y * l;
                dir[0] = l * x;
                dir[2] = z * l;
            } else {
                dir[0] = x;
                dir[1] = y;
                dir[2] = z;
            }
        };

        // emission shape (+0x1f0) inside the bounds +0x1b0..+0x1c4
        float mnx = d.f(0x1b0), mny = d.f(0x1b4), mnz = d.f(0x1b8), mxx = d.f(0x1bc), mxy = d.f(0x1c0), mxz = d.f(0x1c4);
        float px = 0, py = 0, pz = 0;
        uint32_t shape = d.u(0x1f0), mode = d.u(0x1f4);
        if (shape == 0) {  // box: volume (mode 0) or surface (mode 1)
            int r5 = R.next() % 5;
            int axis = r5 % 3;
            if (mode == 0) {
                i = R.next();
                px = static_cast<float>(i) * kRandScale * (mxx - mnx) + mnx;
                i = R.next();
                py = static_cast<float>(i) * kRandScale * (mxy - mny) + mny;
                i = R.next();
                pz = static_cast<float>(i) * kRandScale * (mxz - mnz) + mnz;
            } else if (mode == 1) {
                float tt[3] = {0, 0, 0};
                tt[axis] = r5 > 2 ? 1.0f : 0.0f;
                tt[(axis + 1) % 3] = frand(R);
                tt[(axis + 2) % 3] = frand(R);
                px = mnx + tt[0] * (mxx - mnx);
                py = mny + tt[1] * (mxy - mny);
                pz = mnz + tt[2] * (mxz - mnz);
            }
            if (dirMode == 1) outward(px, py, pz);
        } else if (shape == 4) {  // cylinder (y axis): surface incl. caps (mode 1) or volume
            float cx = (mnx + mxx) * 0.5f, cz = (mnz + mxz) * 0.5f;
            float rad = d.f(0x1cc);
            if (rad <= 0.0f) {
                float dz = mxz - mnz, dx = mxx - mnx;
                rad = std::sqrt(dz * dz + dx * dx) * 0.5f;
            }
            float a, b;
            if (mode == 1) {
                float cap = rad * 3.1415927f * rad;
                cap = (1.0f / (std::fabs(mxy - mny) * rad * 6.2831855f + cap * 2.0f)) * cap;
                i = R.next();
                if (cap + cap <= static_cast<float>(i) * kRandScale) {
                    i = R.next();
                    float ang = static_cast<float>(i) * 0.00019175345f;
                    a = std::cos(ang);
                    i = R.next();
                    py = static_cast<float>(i) * kRandScale * (mxy - mny) + mny;
                    b = std::sin(ang);
                } else {
                    do {
                        a = signedRand(R);
                        b = signedRand(R);
                    } while (1.0f < b * b + a * a);
                    // caps: a -> x, b -> z
                    pz = b * rad + cz;
                    px = a * rad + cx;
                    py = signedRand(R) < 0.0f ? mny : mxy;
                    if (dirMode == 1) outward(px, py, pz);
                    goto place;
                }
            } else {
                do {
                    a = signedRand(R);
                    b = signedRand(R);
                } while (1.0f < b * b + a * a);
                i = R.next();
                py = static_cast<float>(i) * kRandScale * (mxy - mny) + mny;
            }
            pz = b * rad + cz;
            px = a * rad + cx;
            if (dirMode == 1) outward(px, py, pz);
        } else if (shape == 5) {  // disc (FUN_18011eb30) scaled to the bounds, height uniform
            R.next();
            i = R.next();
            float ang = static_cast<float>(i) * kRandScale * 6.2831855f - 3.1415927f;
            float c = std::cos(ang), s = std::sin(ang);
            float rr;
            if (mode == 0) {
                rr = frand(R);
            } else {
                float inner = 1.0f - d.f(0x1f8);
                i = R.next();
                rr = static_cast<float>(i) * kRandScale * (1.0f - inner) + inner;
            }
            float ox = c * rr, oz = s * rr;
            pz = std::fabs(mxz - mnz) * oz + (mxz + mnz) * 0.5f;
            i = R.next();
            py = static_cast<float>(i) * kRandScale * (mxy - mny) + mny;
            px = std::fabs(mxx - mnx) * ox + (mxx + mnx) * 0.5f;
        } else {
            static bool warned = false;
            if (!warned) std::fprintf(stderr, "warning: particle emitter shape %u not implemented\n", shape);
            warned = true;
        }
    place:
        const float* m = M.m;
        P.pos[2] = py * m[9] + px * m[8] + pz * m[10] + m[11];
        P.pos[0] = py * m[1] + px * m[0] + pz * m[2] + m[3];
        P.pos[1] = py * m[5] + px * m[4] + pz * m[6] + m[7];
        // velocity = emitter rotation applied to the direction, times speed
        float qx = q.x, qy = q.y, qz = q.z;
        float tx = qy * dir[2] - qz * dir[1];
        float ty = qz * dir[0] - qx * dir[2];
        float tz = qx * dir[1] - qy * dir[0];
        float ux = tz * qy - ty * qz;
        float uy = tx * qz - tz * qx;
        float uz = ty * qx - tx * qy;
        float w2 = q.w + q.w;
        P.vel[0] = (tx * w2 + dir[0] + ux * 2.0f) * speed;
        P.vel[1] = (ty * w2 + dir[1] + uy * 2.0f) * speed;
        P.vel[2] = (tz * w2 + dir[2] + uz * 2.0f) * speed;
        P.prev[0] = P.pos[0];
        P.prev[1] = P.pos[1];
        P.prev[2] = P.pos[2];
        P.ageRate = 1.0f;
        p_.push_back(P);
    }
}

void ParticleEmitter::update(float dt, const Vec3& /*worldPos*/, const Quat& worldRot, const Mat4& worldMatrix) {
    if (!enabled) return;
    const ParticleSystemResource& d = *res_;
    uint32_t maxN = d.u(0x14);
    uint32_t burst = d.u(0x10) < maxN ? d.u(0x10) : maxN;
    if (age_ == 0.0f) {
        if (!warmup_ || dt <= 0.0f) {
            if (burst) emit(burst, worldRot, worldMatrix);
        } else {
            p_ = d.map;  // pre-simulated state
        }
    }
    float a = age_;
    age_ = dt + a;
    if (d.f(0xc) == 0.0f || dt + a < d.f(0xc)) {
        float r = customRate ? rate : d.f(0x8);
        accum_ = dt * r + accum_;
        if (1.0f <= accum_ && p_.size() < maxN) {
            uint32_t n = static_cast<uint32_t>(static_cast<int64_t>(accum_));
            if (maxN != 0 && maxN - static_cast<uint32_t>(p_.size()) < n) n = maxN - static_cast<uint32_t>(p_.size());
            if (n) {
                emit(n, worldRot, worldMatrix);
                emitted_ += n;
                accum_ = accum_ - static_cast<float>(n);
            }
        }
    }
    integrate(dt);
}

// SGParticleEmitter::integrateParticles: age, fields, semi-implicit Euler, colliders; dead
// particles are replaced by the last one (iteration runs backwards)
void ParticleEmitter::integrate(float dtIn) {
    const ParticleSystemResource& d = *res_;
    float dt = std::fabs(dtIn);
    uint32_t lifeMode = d.u(0x1fc);
    boundsValid_ = false;
    const FastNoise& noise = fastNoise();
    for (int i = static_cast<int>(p_.size()) - 1; i >= 0; --i) {
        Particle& P = p_[static_cast<size_t>(i)];
        float rate = P.ageRate;
        float a = rate * dt + P.age;
        P.age = a;
        if (lifeMode != 1) {
            if (lifeMode == 3) {
                if (0.0f <= rate) {
                    if (P.life < a) { P.age = P.life; P.ageRate = -rate; }
                } else if (a < 0.0f) {
                    P.age = 0;
                    P.ageRate = -rate;
                }
            } else if (P.life <= a && a != P.life) {
                if (lifeMode == 2) P.age = 0;
                else if (lifeMode == 0) P.age = kDead;
            }
        }
        if (P.age <= 0.0f) P.age = 0.0f;
        if (lifeMode != 3 && P.ageRate < 0.0f) P.ageRate = -P.ageRate;
        bool dead = P.age == kDead;
        if (!dead) {
            if (0.0f < d.f(0x1dc)) {  // flipbook frame rate
                float frames = static_cast<float>(d.u(0x1d8));
                float f = d.f(0x1dc) * dt + P.frame;
                if (d.b(0x1e8) == 0) {
                    if (frames - 1.0f <= f) f = frames - 1.0f;
                } else {
                    for (; frames < f; f = f - frames) {}
                }
                P.frame = f;
            }
            float fx = 0, fy = 0, fz = 0;
            for (const ParticleField& fl : d.fields) {
                float g[3] = {0, 0, 0};
                if (fl.type == ParticleField::Force) {
                    const float* m = fl.matrix;  // identity unless the field follows a matrix
                    g[0] = m[1] * fl.force[1] + m[0] * fl.force[0] + m[2] * fl.force[2];
                    g[1] = m[5] * fl.force[1] + m[4] * fl.force[0] + m[6] * fl.force[2];
                    g[2] = m[9] * fl.force[1] + m[8] * fl.force[0] + m[10] * fl.force[2];
                } else if (fl.type == ParticleField::Turbulence3D) {
                    const float* t = fl.turb;  // freq, strength, phase, falloff hi/lo, axis scales
                    float sx = t[0] * P.pos[0];
                    float ph = t[2] * P.age;
                    float sy = P.pos[1] * t[0];
                    float sz = t[0] * P.pos[2];
                    float fall = std::fabs(P.pos[1] - t[4]) / (t[3] - t[4]);
                    if (1.0f <= fall) fall = 1.0f;
                    if (fall <= 0.0f) fall = 0.0f;
                    float amp = fall * t[1] * dt;
                    float nz = noise.noise(sz, sx + ph, sy);
                    float ny = noise.noise(sy, sz + ph, sx);
                    float nx = noise.noise(sx, sy + ph, sz);
                    g[1] = ny * amp * t[6];
                    g[2] = nz * amp * t[7];
                    g[0] = nx * amp * t[5];
                }
                fx = fx + g[0];
                fy = fy + g[1];
                fz = fz + g[2];
            }
            float drag = P.drag, im = P.invMass;
            P.prev[0] = P.pos[0];
            P.prev[1] = P.pos[1];
            P.prev[2] = P.pos[2];
            float ay = (fy + drag * P.vel[1]) * im * dt;
            float ax = im * (fx + drag * P.vel[0]) * dt;
            float az = (fz + drag * P.vel[2]) * im * dt;
            P.pos[0] = (ax * 0.5f + P.vel[0]) * dt + P.pos[0];
            P.pos[1] = (ay * 0.5f + P.vel[1]) * dt + P.pos[1];
            P.pos[2] = (az * 0.5f + P.vel[2]) * dt + P.pos[2];
            P.vel[0] = ax + P.vel[0];
            P.vel[1] = ay + P.vel[1];
            P.vel[2] = az + P.vel[2];
            bool kill = d.b(0x280) != 0;
            for (const ParticleCollider& c : d.colliders) {  // PlaneCollider::collideParticle
                const float* n = c.normal;
                float dist = n[1] * P.pos[1] + P.pos[0] * n[0] + n[2] * P.pos[2] + c.d;
                if (dist < 0.0f) {
                    if (P.prev[1] * n[1] + n[0] * P.prev[0] + P.prev[2] * n[2] + c.d < 0.0f || kill) {
                        P.age = kDead;
                        continue;
                    }
                    float vy = -P.vel[1], vz = -P.vel[2], vx = -P.vel[0];
                    float l = std::sqrt(vy * vy + vx * vx + vz * vz);
                    if (0.0f < l) {
                        l = 1.0f / l;
                        vx *= l;
                        vy *= l;
                        vz *= l;
                    }
                    float s = (-1.0f / (vy * n[1] + vx * n[0] + vz * n[2])) * dist;
                    P.pos[0] = P.pos[0] + vx * s;
                    P.pos[1] = vy * s + P.pos[1];
                    P.pos[2] = vz * s + P.pos[2];
                    float vn = P.vel[1] * n[1] + n[0] * P.vel[0] + P.vel[2] * n[2];
                    P.vel[0] = P.vel[0] - n[0] * 2.0f * vn;
                    P.vel[1] = P.vel[1] - (n[1] + n[1]) * vn;
                    P.vel[2] = P.vel[2] - (n[2] + n[2]) * vn;
                    float res = c.resilience <= 0.0f ? 0.0f : c.resilience;
                    P.vel[0] *= res;
                    P.vel[1] *= res;
                    P.vel[2] *= res;
                }
            }
            dead = P.age == kDead;
        }
        if (dead) {
            size_t last = p_.size() - 1;
            if (static_cast<size_t>(i) < last) p_[static_cast<size_t>(i)] = p_[last];
            p_.pop_back();
            continue;
        }
        Vec3 v(P.pos[0], P.pos[1], P.pos[2]);
        if (!boundsValid_) {
            lo_ = hi_ = v;
            boundsValid_ = true;
        } else {
            lo_ = Vec3(std::min(lo_.x, v.x), std::min(lo_.y, v.y), std::min(lo_.z, v.z));
            hi_ = Vec3(std::max(hi_.x, v.x), std::max(hi_.y, v.y), std::max(hi_.z, v.z));
        }
    }
}

}  // namespace oyster
