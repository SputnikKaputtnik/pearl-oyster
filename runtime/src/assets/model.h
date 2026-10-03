// .mxm model resource (MOXIE::ModelResource::read @0x180144250).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "assets/common.h"
#include "assets/geometry.h"
#include "core/math.h"

namespace oyster {

struct SubMesh {  // 12-byte entry at the end of a mesh
    uint32_t material = 0;
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
};

// Subdivision surface baked into linear stencils (SubdivSurfaceInstance, see engine-internals.md):
// out_j = sum_{i<k_j} w_i * cage[idx_i] for position and normal, then render[v] = out[remap[v]].
struct SubdivSurface {
    uint32_t cageVertices = 0;
    uint32_t attrFlags = 0;
    std::vector<float> cagePosition;  // 3 per cage vertex (rest pose)
    std::vector<float> cageNormal;    // 3 per cage vertex
    std::vector<uint8_t> stencilSize; // one per output point
    std::vector<uint32_t> stencilIndex;
    std::vector<float> stencilWeight;
    std::vector<uint32_t> remap;      // render vertex -> output point
    uint32_t topo[8] = {};
};

struct Mesh {
    uint32_t u11c = 0, u120 = 0;
    std::vector<uint16_t> nodes;      // transform node(s) the mesh is attached to (Pearl: one)
    uint32_t instanceCount = 0;
    std::vector<float> instances;     // per instance: scale(3), rotation xyzw(4), position(3)
    VertexData vertices;
    uint16_t boneGroup = 0xFFFF;      // index into ModelResource::boneGroups (skinned meshes)
    std::vector<Mat4> skinMatrices;   // per palette entry (inverse bind)
    std::vector<uint16_t> indices;
    std::unique_ptr<SubdivSurface> subdiv;
    std::vector<SubMesh> subMeshes;
    uint64_t indexBits = 0;
};

struct ModelResource {
    uint32_t version = 0;
    uint32_t flags = 0;
    std::string baseModel;            // patch model: geometry from this model
    bool overridesMaterials = false;
    std::vector<Material> materials;
    std::vector<Mat4> localXforms;
    std::vector<std::string> nodeNames;
    std::vector<std::string> nodeProps;
    std::vector<Mat4> bindXforms;
    std::vector<int16_t> parents;
    std::vector<Mesh> meshes;
    std::vector<std::vector<uint16_t>> boneGroups;

    bool isPatch() const { return !baseModel.empty(); }
    int findNode(const std::string& name) const;
};

// Parses a model; geometry decoding can be skipped for fast structural scans.
std::unique_ptr<ModelResource> loadModel(const std::vector<uint8_t>& data, bool decodeGeometry = true);

}  // namespace oyster
