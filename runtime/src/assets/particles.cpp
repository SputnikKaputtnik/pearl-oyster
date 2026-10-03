#include "assets/particles.h"

namespace oyster {

namespace {

// ParticleDefinition::read (0x180124cf0): (struct offset, element size, count) in stream order
struct DefField {
    uint16_t off;
    uint8_t size, count;
};
const DefField kDefLayout[] = {
    {0x00, 4, 2}, {0x08, 4, 1}, {0x0C, 4, 1}, {0x10, 4, 1}, {0x14, 4, 1}, {0x18, 4, 1}, {0x1C, 1, 1},
    {0x20, 4, 1}, {0x24, 4, 3}, {0x30, 1, 1}, {0x34, 4, 16}, {0x74, 4, 1}, {0x78, 4, 4}, {0x88, 4, 3},
    {0x94, 1, 1}, {0x98, 4, 1}, {0x9C, 1, 1}, {0xA0, 4, 1}, {0xA4, 1, 1}, {0xA8, 4, 8}, {0xC8, 4, 1},
    {0xCC, 4, 4}, {0xDC, 4, 3}, {0xE8, 1, 1}, {0xEC, 4, 1}, {0xF0, 1, 1}, {0xF4, 4, 1}, {0xF8, 1, 1},
    {0xFC, 4, 4}, {0x10C, 4, 1}, {0x110, 4, 4}, {0x120, 4, 3}, {0x12C, 1, 1}, {0x130, 4, 1},
    {0x134, 1, 1}, {0x138, 4, 1}, {0x13C, 1, 1}, {0x140, 4, 2}, {0x148, 4, 8}, {0x168, 4, 4},
    {0x178, 4, 2}, {0x180, 4, 6}, {0x198, 4, 2}, {0x1A0, 4, 2}, {0x1A8, 4, 2}, {0x1B0, 4, 6},
    {0x1C8, 4, 1}, {0x1CC, 4, 1}, {0x1D0, 4, 2}, {0x1D8, 4, 1}, {0x1DC, 4, 1}, {0x1E0, 4, 2},
    {0x1E8, 1, 1}, {0x1E9, 1, 1}, {0x1EA, 1, 1}, {0x1EB, 1, 1}, {0x1EC, 1, 1}, {0x1F0, 4, 1},
    {0x1F4, 4, 1}, {0x1F8, 4, 1}, {0x1FC, 4, 1}, {0x200, 4, 1}, {0x204, 4, 1},
};

void readFloats(Reader& r, float* out, size_t n) { r.array(out, n); }

}  // namespace

std::shared_ptr<ParticleSystemResource> loadParticleSystem(const std::vector<uint8_t>& data) {
    Reader r(data);
    if (r.u32() != kMoxieMagic || r.u32() != kTypeParticles) throw FormatError("not a Moxie particle system");
    auto ps = std::make_shared<ParticleSystemResource>();
    ps->version = r.u32();
    ps->name = r.string();
    ps->material = readMaterial(r);
    for (const DefField& f : kDefLayout) r.array(&ps->def[f.off], static_cast<size_t>(f.size) * f.count);
    float version = ps->f(0x204);
    if (version >= 1.9999f) {
        r.skip(256);  // name
        r.array(&ps->def[0x278], 8);
    }
    if (version >= 2.0999f) ps->def[0x280] = r.u8();
    uint16_t nf = r.u16(), nc = r.u16();
    for (uint16_t i = 0; i < nf; ++i) {
        ParticleField fl;
        fl.type = r.u32();
        const uint8_t* nm = r.take(32);
        fl.name.assign(reinterpret_cast<const char*>(nm), strnlen(reinterpret_cast<const char*>(nm), 32));
        switch (fl.type) {
            case ParticleField::Force:
                readFloats(r, fl.force, 3);
                fl.useMatrix = r.u8();
                readFloats(r, fl.matrix, 16);
                break;
            case ParticleField::Turbulence3D: readFloats(r, fl.turb, 8); break;
            case ParticleField::Turbulence2D: readFloats(r, fl.turb, 5); break;
            case ParticleField::Bobber: readFloats(r, fl.turb, 6); break;
            default: throw FormatError("unknown particle field type");
        }
        ps->fields.push_back(fl);
    }
    for (uint16_t i = 0; i < nc; ++i) {
        ParticleCollider c;
        c.type = r.u32();
        if (c.type != 0) throw FormatError("unknown particle collider type");
        const uint8_t* nm = r.take(32);
        c.name.assign(reinterpret_cast<const char*>(nm), strnlen(reinterpret_cast<const char*>(nm), 32));
        readFloats(r, c.normal, 3);
        c.d = r.f32();
        c.resilience = r.f32();
        c.f80 = r.f32();
        c.f84 = r.f32();
        readFloats(r, c.point, 3);
        ps->colliders.push_back(c);
    }
    if (ps->version == 2) {
        uint32_t bytes = r.u32(), count = r.u32();
        if (bytes && count) {
            if (bytes != count * sizeof(Particle)) throw FormatError("particle map size mismatch");
            ps->map.resize(count);
            r.array(reinterpret_cast<uint8_t*>(ps->map.data()), bytes);
        }
    }
    return ps;
}

}  // namespace oyster
