#include "assets/shader_file.h"

#include "core/reader.h"

namespace oyster {

ShaderFile loadShaderFile(const std::vector<uint8_t>& data) {
    Reader r(data);
    if (r.u32() != 0x10001) throw FormatError("bad .shd magic");
    ShaderFile s;
    r.u8();
    r.u32();
    uint32_t size = r.u32();
    r.u32();
    r.u32();
    const char* raw = reinterpret_cast<const char*>(r.take(size));
    std::vector<std::string> parts(1);
    for (uint32_t i = 0; i < size; ++i) {
        if (raw[i] == 0) parts.emplace_back();
        else parts.back() += raw[i];
    }
    s.master = parts.size() > 2 ? parts[2] : parts[0];
    r.skip(12);
    uint32_t nprog = r.u32();
    r.skip(8);
    std::vector<std::string> progs;
    for (uint32_t i = 0; i < nprog; ++i) {
        uint32_t n = r.u32();
        progs.emplace_back(reinterpret_cast<const char*>(r.take(n)), n);
    }
    if (progs.size() < 2) throw FormatError("shader without two programs");
    s.vs = progs[0];
    s.fs = progs[1];
    s.flags = r.u32();
    s.hash = r.u32();
    s.nparams = r.u32();
    uint32_t nsem = r.u32();
    for (uint32_t i = 0; i < s.nparams + nsem; ++i) {
        ShaderUniform u;
        u.cls = r.u16();
        u.type = r.u16();
        u.semantic = r.u16();
        u.count = r.u16();
        u.size = r.u16();
        u.size2 = r.u16();
        u.name = r.string();
        if (u.cls >= 3) u.unit = static_cast<int32_t>(r.u32());
        s.uniforms.push_back(std::move(u));
    }
    if (!r.eof()) throw FormatError("trailing bytes in shader");
    return s;
}

}  // namespace oyster
