// DDS textures as shipped with Pearl: DXT5 (2027), RGBA8 (197), ETC2 RGB (8), ETC2 RGBA "ETA8" (9).
// The header never declares mip levels (dwMipMapCount = 0) and the original samples level 0
// only (no mipmaps observed in the GL trace), so only level 0 is loaded.
#pragma once
#include <cstdint>
#include <vector>

namespace oyster {

enum class TexFormat { RGBA8, DXT5, ETC2_RGB8, ETC2_RGBA8 };

struct TextureData {
    uint32_t width = 0, height = 0;
    TexFormat format = TexFormat::RGBA8;
    std::vector<uint8_t> level0;
    bool compressed() const { return format != TexFormat::RGBA8; }
};

TextureData loadDDS(const std::vector<uint8_t>& data);

// Lossless software decode of DXT5/BC3 to RGBA8 for GPUs without S3TC (Quest / Adreno).
// `rounding` selects how the interpolated colours are rounded (see dds.cpp); the default is
// the variant that reproduces the desktop GPU the original ran on.
void decodeDXT5(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* rgba, int rounding = 4);

}  // namespace oyster
