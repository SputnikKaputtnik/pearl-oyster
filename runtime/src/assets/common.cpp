#include "assets/common.h"

namespace oyster {

Uri readUri(Reader& r) {  // MOXIE::Uri::read
    Uri u;
    u.kind = r.u8();
    u.hash = r.u32();
    uint32_t size = r.u32();
    r.u32();
    r.u32();
    const char* raw = reinterpret_cast<const char*>(r.take(size));
    // "pkg\0path\0pkg:path\0"
    std::vector<std::string> parts;
    std::string cur;
    for (uint32_t i = 0; i < size; ++i) {
        if (raw[i] == 0) { parts.push_back(cur); cur.clear(); }
        else cur += raw[i];
    }
    if (!cur.empty()) parts.push_back(cur);
    u.uri = parts.size() > 2 ? parts[2] : (parts.empty() ? std::string() : parts.back());
    r.u32();
    r.u32();
    r.u32();
    return u;
}

uint32_t glEnumFromIndex(uint32_t i) {
    static const uint32_t table[] = {
        0x0000, 0x0001, 0x0300, 0x0302, 0x0306, 0x0304, 0x0301, 0x0303, 0x0307, 0x0305, 0x0308,
        0x8001, 0x8003, 0x8006, 0x800A, 0x800B, 0x8007, 0x8008,
        0x0200, 0x0207, 0x0201, 0x0203, 0x0204, 0x0206, 0x0205,
        0x1E00, 0x1E01, 0x150A, 0x1E02, 0x1E03,
        0x0404, 0x0405, 0x0408, 0x0900, 0x0901,
        0x1B00, 0x1B01, 0x1B02};
    return i < sizeof(table) / sizeof(table[0]) ? table[i] : 0;
}

RenderState readRenderState(Reader& r) {
    const uint8_t* b = r.take(0x94);
    auto u = [&](int o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto f = [&](int o) { float v; std::memcpy(&v, b + o, 4); return v; };
    RenderState s;
    s.blend = b[0] != 0;
    s.blendSrcRGB = u(4); s.blendSrcAlpha = u(8); s.blendDstRGB = u(0xC); s.blendDstAlpha = u(0x10);
    for (int i = 0; i < 4; ++i) s.blendColor[i] = f(0x14 + 4 * i);
    s.blendEqRGB = u(0x24); s.blendEqAlpha = u(0x28);
    s.depthTest = b[0x2C] != 0; s.depthWrite = b[0x2D] != 0; s.depthFunc = u(0x30);
    s.depthRange[0] = f(0x34); s.depthRange[1] = f(0x38);
    s.scissor = b[0x3C] != 0;
    for (int i = 0; i < 4; ++i) s.scissorRect[i] = static_cast<int32_t>(u(0x40 + 4 * i));
    s.stencil = b[0x50] != 0;
    s.stencilMask = u(0x54); s.stencilFunc = u(0x58); s.stencilRef = u(0x5C); s.stencilReadMask = u(0x60);
    s.stencilOps[0] = u(0x64); s.stencilOps[1] = u(0x68); s.stencilOps[2] = u(0x6C);
    for (int i = 0; i < 4; ++i) s.colorMask[i] = b[0x70 + i] != 0;
    s.cull = b[0x74] != 0; s.cullFace = u(0x78); s.polygonMode = u(0x7C);
    s.polygonOffset = b[0x80] != 0; s.polygonOffsetFactor = f(0x84); s.polygonOffsetUnits = f(0x88);
    s.dither = b[0x8C] != 0; s.f90 = f(0x90);
    return s;
}

static MaterialParam readParam(Reader& r) {
    MaterialParam p;
    p.type = r.u16();
    p.nameHash = r.u32();
    p.uniformIndex = r.u16();
    p.size = r.u32();
    p.u14 = r.u32();
    p.u1c = r.u8();
    if (p.type == 0) {
        p.values = r.vec<float>(p.size / 4);
    } else {
        p.texture = readUri(r);
        p.texKind = r.u16();
        uint32_t n = r.u32();
        for (uint32_t i = 0; i < n; ++i) {
            Sampler s;
            s.minFilter = r.u32(); s.magFilter = r.u32(); s.mipFilter = r.u32();
            s.wrapS = r.u32(); s.wrapT = r.u32(); s.wrapR = r.u32();
            s.f = r.f32();
            p.samplers.push_back(s);
        }
    }
    return p;
}

Material readMaterial(Reader& r) {
    uint32_t ver = r.u32();
    if (ver != 2) throw FormatError("material version " + std::to_string(ver));
    Material m;
    m.name = r.string();
    m.nameHash = fnv1a(m.name);
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        MaterialPass pass;
        pass.shader = readUri(r);
        pass.state = readRenderState(r);
        pass.ud4 = r.u32();
        uint32_t np = r.u32();
        for (uint32_t k = 0; k < np; ++k) pass.params.push_back(readParam(r));
        m.passes.push_back(std::move(pass));
    }
    return m;
}

}  // namespace oyster
