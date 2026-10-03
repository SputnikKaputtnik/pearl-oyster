// Compressed vertex/index streams of .mxm meshes:
// VertexCompression::decompress (0x1800bd3e0) / decompressIndices (0x1800bdc90).
#pragma once
#include <cstdint>
#include <vector>

namespace oyster {

struct VertexData {
    uint32_t count = 0;
    uint32_t flags = 0;  // bit0 pos, 1 normal, 2 tangent, 3..6 uv0..3, 7 color, 8 weights, 9 joints
    std::vector<float> position;  // 3 per vertex
    std::vector<float> normal;    // 3
    std::vector<float> tangent;   // 3
    std::vector<float> color;     // 4
    std::vector<float> uv[4];     // 2 each
    std::vector<float> weights;   // 4, renormalized to sum 1
    std::vector<float> joints;    // 4 (stored as float for the a_blendIndices attribute)
    uint64_t bitsUsed = 0;
};

VertexData decodeVertices(const uint8_t* blob, size_t size);
std::vector<uint16_t> decodeIndices(const uint8_t* blob, size_t size, uint64_t* bitsUsed = nullptr);

}  // namespace oyster
