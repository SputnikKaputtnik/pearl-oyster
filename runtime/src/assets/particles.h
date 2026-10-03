// .mxb particle systems (MOXIE::ParticleSystemResource::onLoad, see docs/file-formats.md and
// docs/particles.md). The ParticleDefinition is kept as its in-memory byte image so the
// simulation can address fields by the engine's own offsets.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "assets/common.h"

namespace oyster {

// MOXIE::Particle, 0x50 bytes (also the record of the pre-simulated map in the file)
struct Particle {
    float pos[3];     // +0x00
    float age;        // +0x0c
    float vel[3];     // +0x10
    float rot;        // +0x1c
    float prev[3];    // +0x20
    uint8_t col[4];   // +0x2c RGBA8
    float size[2];    // +0x30
    float life;       // +0x38
    float frame;      // +0x3c
    float invMass;    // +0x40
    float drag;       // +0x44
    float rnd;        // +0x48
    float ageRate;    // +0x4c
};
static_assert(sizeof(Particle) == 0x50, "Particle layout");

struct ParticleField {
    enum Type : uint32_t { Force = 0, Turbulence3D = 1, Turbulence2D = 2, Bobber = 3 };
    uint32_t type = 0;
    std::string name;
    // Force: force (0x80), flag (0x8c), matrix (0x90); Turbulence3D: 8 floats at 0x80..0x9c
    float force[3] = {};
    uint8_t useMatrix = 0;
    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float turb[8] = {};
};

struct ParticleCollider {  // PlaneCollider
    uint32_t type = 0;
    std::string name;
    float normal[3] = {0, 1, 0};  // +0x98
    float d = 0;                  // +0xa4
    float resilience = 0;         // +0x88
    float f80 = 0, f84 = 0;       // +0x80, +0x84 (not used by collideParticle)
    float point[3] = {};          // +0x8c
};

struct ParticleSystemResource {
    uint32_t version = 0;
    std::string name;
    Material material;
    std::array<uint8_t, 0x290> def{};  // ParticleDefinition image
    std::vector<ParticleField> fields;
    std::vector<ParticleCollider> colliders;
    std::vector<Particle> map;  // pre-simulated particles (warm-up state)

    float f(size_t off) const { float v; std::memcpy(&v, &def[off], 4); return v; }
    uint32_t u(size_t off) const { uint32_t v; std::memcpy(&v, &def[off], 4); return v; }
    uint8_t b(size_t off) const { return def[off]; }
};

std::shared_ptr<ParticleSystemResource> loadParticleSystem(const std::vector<uint8_t>& data);

}  // namespace oyster
