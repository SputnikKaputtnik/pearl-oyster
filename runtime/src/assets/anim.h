// .mxa animation resource (MOXIE::AnimResource::read @0x180140c40) and VANM vertex animation
// decoding (VertexAnimQuantizer / VertexAnimDecoder, see docs/file-formats.md).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "assets/common.h"

namespace oyster {

struct AnimKey {
    uint32_t frame = 0;
    uint32_t offset = 0;  // into Track::blob
    uint32_t stepBits = 0;  // per-component "stepped" bits (vec3: bit0..2; quat/scalar: != 0)
};

enum class ChannelValue : uint32_t { Scalar0 = 0, Scalar1 = 1, Scalar2 = 2, Vec2 = 3, Vec3 = 4, Vec4 = 5, Quat = 6, None = 0xFFFFFFFF };

struct AnimChannel {
    uint32_t attrHash = 0;  // FNV-1a: "translation", "rotation", "scale", ...
    uint32_t valueType = 0;
    std::vector<AnimKey> keys;
    // custom channels (animated material parameters):
    uint32_t customA = 0;
    std::vector<uint32_t> customVals;  // [materialNameHash, paramNameHash, passIndex, component]
};

struct AnimTrack {
    std::string name;  // node name (Maya DAG path), or "__moxie__<material>|RootNode"
    std::vector<uint8_t> blob;
    std::vector<AnimChannel> channels;
    std::vector<AnimChannel> custom;
};

// One compressed per-vertex channel (positions or normals) of a vertex-animated mesh.
struct VanmChunk {
    uint32_t a = 0;
    uint32_t frames = 0;
    uint32_t vertexCount = 0;
    std::vector<std::vector<uint8_t>> blockHeaders;  // one per 16 frames
    std::vector<uint64_t> blockHeaderBits;
    std::vector<uint8_t> data;
    uint64_t dataBits = 0;
};

struct VanmMesh {
    std::string name;  // = model node name
    uint32_t mode = 3; // bit 1 set: stepped playback; else linear blend
    uint32_t frames = 0;
    uint8_t field8[8] = {};
    std::vector<uint16_t> visibility;  // 1 bit per frame, 16 frames per block
    std::vector<float> bounds;         // 6 per block
    std::vector<VanmChunk> chunks;     // [0] = positions, [1] = normals
    bool stepped() const { return (mode & 2) != 0; }
    bool visible(uint32_t f) const {
        uint32_t b = f >> 4;
        return b < visibility.size() && ((visibility[b] >> (f & 15)) & 1);
    }
};

struct VertexAnim {
    uint32_t version = 0, u = 0;
    float fps = 30.0f;  // not used for playback (header integer fps is)
    std::vector<VanmMesh> meshes;
};

struct AnimResource {
    uint32_t version = 0;
    uint32_t frames = 0;
    uint32_t fps = 30;
    std::vector<AnimTrack> tracks;
    uint32_t u50 = 0;
    std::unique_ptr<VertexAnim> vertexAnim;
};

std::unique_ptr<AnimResource> loadAnim(const std::vector<uint8_t>& data);

// Incremental decoder of one VANM chunk, like VertexAnimDecoder: inside a 16-frame block frames
// are decoded forward from the block's absolute first frame (frames 1..15 are deltas).
class VanmDecoder {
public:
    explicit VanmDecoder(const VanmChunk* chunk) : c_(chunk) {}
    // Returns 3 floats per vertex for frame f (clamped to the chunk's frame range).
    const std::vector<float>& frame(uint32_t f);
    uint64_t maxDataBit() const { return maxBit_; }

private:
    struct BlockHeader {
        uint32_t n = 0;
        std::vector<uint32_t> offsets;  // per frame
        std::vector<uint32_t> widths;   // 3 per frame
        std::vector<float> mn, mx, off; // 3 per frame
    };
    void parseHeader(uint32_t block);
    void decodeInto(uint32_t frameInBlock, bool delta);

    const VanmChunk* c_;
    BlockHeader h_;
    int32_t block_ = -1;
    int32_t last_ = -1;  // last decoded frame inside block_
    std::vector<float> cur_;
    uint64_t maxBit_ = 0;
};

}  // namespace oyster
