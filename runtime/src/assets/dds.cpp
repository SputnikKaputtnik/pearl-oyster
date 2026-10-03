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

namespace {

inline void expand565(uint16_t c, int rgb[3]) {
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    rgb[0] = (r << 3) | (r >> 2);
    rgb[1] = (g << 2) | (g >> 4);
    rgb[2] = (b << 3) | (b >> 2);
}

}  // namespace

void decodeDXT5(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* rgba, int rounding) {
    uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4;
    for (uint32_t by = 0; by < bh; ++by)
        for (uint32_t bx = 0; bx < bw; ++bx, src += 16) {
            // alpha: two endpoints + 3-bit indices (8- or 6-value mode)
            int a0 = src[0], a1 = src[1];
            int alpha[8];
            alpha[0] = a0;
            alpha[1] = a1;
            if (a0 > a1) {
                for (int i = 1; i < 7; ++i)
                    alpha[i + 1] = (rounding == 1 || rounding == 4) ? ((7 - i) * a0 + i * a1 + 3) / 7 : ((7 - i) * a0 + i * a1) / 7;
            } else {
                for (int i = 1; i < 5; ++i)
                    alpha[i + 1] = (rounding == 1 || rounding == 4) ? ((5 - i) * a0 + i * a1 + 2) / 5 : ((5 - i) * a0 + i * a1) / 5;
                alpha[6] = 0;
                alpha[7] = 255;
            }
            uint64_t abits = 0;
            for (int i = 0; i < 6; ++i) abits |= static_cast<uint64_t>(src[2 + i]) << (8 * i);
            // colour: two RGB565 endpoints, always the four-colour mode in BC3
            uint16_t c0 = static_cast<uint16_t>(src[8] | (src[9] << 8)), c1 = static_cast<uint16_t>(src[10] | (src[11] << 8));
            int col[4][3];
            expand565(c0, col[0]);
            expand565(c1, col[1]);
            for (int k = 0; k < 3; ++k) {
                if (rounding == 1) {
                    col[2][k] = (2 * col[0][k] + col[1][k] + 1) / 3;
                    col[3][k] = (col[0][k] + 2 * col[1][k] + 1) / 3;
                } else if (rounding >= 3) {
                    // NVIDIA hardware arithmetic (as documented by rgbcx, "bc1_approx_mode NVidia"):
                    // red/blue from the 5-bit endpoints, green from the expanded 8-bit values
                    if (k == 1) {
                        int gd = col[1][1] - col[0][1];
                        col[2][1] = (256 * col[0][1] + gd / 4 + 128 + gd * 80) / 256;
                        col[3][1] = (256 * col[1][1] - gd / 4 + 128 - gd * 80) / 256;
                    } else {
                        int sh = k == 0 ? 11 : 0;
                        int e0 = (c0 >> sh) & 31, e1 = (c1 >> sh) & 31;
                        col[2][k] = ((2 * e0 + e1) * 22) / 8;
                        col[3][k] = ((2 * e1 + e0) * 22) / 8;
                    }
                } else if (rounding == 2) {  // 3/8 + 5/8 weights in 1/8 steps
                    col[2][k] = (5 * col[0][k] + 3 * col[1][k] + 4) / 8;
                    col[3][k] = (3 * col[0][k] + 5 * col[1][k] + 4) / 8;
                } else {
                    col[2][k] = (2 * col[0][k] + col[1][k]) / 3;
                    col[3][k] = (col[0][k] + 2 * col[1][k]) / 3;
                }
            }
            uint32_t cbits = static_cast<uint32_t>(src[12]) | (static_cast<uint32_t>(src[13]) << 8) |
                             (static_cast<uint32_t>(src[14]) << 16) | (static_cast<uint32_t>(src[15]) << 24);
            for (int py = 0; py < 4; ++py)
                for (int px = 0; px < 4; ++px) {
                    uint32_t x = bx * 4 + static_cast<uint32_t>(px), y = by * 4 + static_cast<uint32_t>(py);
                    int i = py * 4 + px;
                    if (x >= width || y >= height) continue;
                    uint8_t* o = rgba + (static_cast<size_t>(y) * width + x) * 4;
                    const int* c = col[(cbits >> (2 * i)) & 3];
                    o[0] = static_cast<uint8_t>(c[0]);
                    o[1] = static_cast<uint8_t>(c[1]);
                    o[2] = static_cast<uint8_t>(c[2]);
                    o[3] = static_cast<uint8_t>(alpha[(abits >> (3 * i)) & 7]);
                }
        }
}

}  // namespace oyster
