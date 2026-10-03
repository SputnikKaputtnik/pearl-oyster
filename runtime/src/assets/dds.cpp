#include "assets/dds.h"

#include <cstring>

#include "core/reader.h"

namespace oyster {

TextureData loadDDS(const std::vector<uint8_t>& data) {
    if (data.size() < 128 || std::memcmp(data.data(), "DDS ", 4) != 0) throw FormatError("not a DDS file");
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, data.data() + o, 4); return v; };
    TextureData t;
    t.height = u32(12);
    t.width = u32(16);
    uint32_t pfFlags = u32(80);
    char fourcc[5] = {0};
    std::memcpy(fourcc, data.data() + 84, 4);
    size_t bytes;
    uint32_t bw = (t.width + 3) / 4, bh = (t.height + 3) / 4;
    if (pfFlags & 4) {
        if (!std::strcmp(fourcc, "DXT5")) { t.format = TexFormat::DXT5; bytes = 16ull * bw * bh; }
        else if (!std::strcmp(fourcc, "ETC2")) { t.format = TexFormat::ETC2_RGB8; bytes = 8ull * bw * bh; }
        else if (!std::strcmp(fourcc, "ETA8")) { t.format = TexFormat::ETC2_RGBA8; bytes = 16ull * bw * bh; }
        else throw FormatError(std::string("unsupported DDS FourCC ") + fourcc);
    } else {
        uint32_t bpp = u32(88), rmask = u32(92), amask = u32(104);
        if (bpp != 32 || rmask != 0xFF || amask != 0xFF000000u) throw FormatError("unsupported uncompressed DDS layout");
        t.format = TexFormat::RGBA8;
        bytes = 4ull * t.width * t.height;
    }
    if (data.size() < 128 + bytes) throw FormatError("DDS truncated");
    t.level0.assign(data.begin() + 128, data.begin() + 128 + static_cast<std::ptrdiff_t>(bytes));
    return t;
}

}  // namespace oyster
