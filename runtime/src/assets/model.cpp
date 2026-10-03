#include "assets/model.h"

namespace oyster {

namespace {

const uint32_t kSubdivComponents[8] = {3, 3, 4, 2, 2, 2, 2, 4};  // pos, normal, tangent, uv0..3, color

std::unique_ptr<SubdivSurface> readSubdiv(Reader& r) {  // SubdivSurface::read -> FUN_1801b7340
    auto s = std::make_unique<SubdivSurface>();
    s->cageVertices = r.u32();
    s->attrFlags = r.u32();
    for (int k = 0; k < 8; ++k) {
        if (!(s->attrFlags & (1u << k))) continue;
        size_t n = static_cast<size_t>(kSubdivComponents[k]) * s->cageVertices;
        if (k == 0) s->cagePosition = r.vec<float>(n);
        else if (k == 1) s->cageNormal = r.vec<float>(n);
        else r.skip(4 * n);
    }
    if (r.u8()) r.skip(static_cast<size_t>(20) * s->cageVertices);  // skin: 4 f32 + 4 u8 per vertex
    r.array(s->topo, 8);
    s->stencilSize = r.vec<uint8_t>(s->topo[0]);
    uint32_t nIdx = r.u32();
    s->stencilIndex = r.vec<uint32_t>(nIdx);
    for (int k = 2; k < 8; ++k)
        if (s->topo[k]) r.skip(4ull * s->topo[k] * kSubdivComponents[k]);  // face-varying values
    s->stencilWeight = r.vec<float>(nIdx);
    r.u32();
    uint32_t b = r.u32();
    r.u32();
    r.skip(4ull * b);
    uint32_t n0 = r.u32(), n1 = r.u32();
    r.skip(8);
    for (uint32_t i = 0; i < n0; ++i) {
        if (i == 0) s->remap = r.vec<uint32_t>(n1);
        else r.skip(4ull * n1);
    }
    return s;
}

// ModelResource::fixupMeshInstances (0x180145470): instances are baked into the buffers at load
// time. Vertex copy k (1..I) = base vertices transformed by instance k-1 (positions by the
// instance matrix, normals/tangents rotated by its quaternion), other attributes copied; each
// submesh's indices are copied to start + N*k (+ n*k), then every submesh range and the index
// count are multiplied by I+1 - with several submeshes the engine's ranges overlap the copies
// of other submeshes; reproduced as is.
void fixupMeshInstances(Mesh& m) {
    const uint32_t I = m.instanceCount;
    VertexData& v = m.vertices;
    const uint32_t n = v.count;
    if (I == 0 || n == 0) return;
    const uint32_t total = n * (I + 1);
    auto grow = [&](std::vector<float>& a, size_t comps) {
        if (!a.empty()) a.resize(static_cast<size_t>(total) * comps);
    };
    grow(v.position, 3);
    grow(v.normal, 3);
    grow(v.tangent, 3);
    grow(v.color, 4);
    for (auto& uv : v.uv) grow(uv, 2);
    grow(v.weights, 4);
    grow(v.joints, 4);
    for (uint32_t k = 1; k <= I; ++k) {
        const float* t = &m.instances[static_cast<size_t>(k - 1) * 10];  // scale(3) quat(4) pos(3)
        Mat4 M = Mat4::trs({t[7], t[8], t[9]}, Quat(t[3], t[4], t[5], t[6]), {t[0], t[1], t[2]});
        const float qx = t[3], qy = t[4], qz = t[5], ww = t[6] + t[6];
        for (uint32_t i = 0; i < n; ++i) {
            const size_t s3 = static_cast<size_t>(i) * 3, d3 = (static_cast<size_t>(k) * n + i) * 3;
            if (!v.position.empty()) {
                const float x = v.position[s3], y = v.position[s3 + 1], z = v.position[s3 + 2];
                v.position[d3] = y * M.m[1] + x * M.m[0] + z * M.m[2] + M.m[3];
                v.position[d3 + 1] = x * M.m[4] + y * M.m[5] + z * M.m[6] + M.m[7];
                v.position[d3 + 2] = x * M.m[8] + y * M.m[9] + z * M.m[10] + M.m[11];
            }
            for (std::vector<float>* vec : {&v.normal, &v.tangent}) {
                if (vec->empty()) continue;
                std::vector<float>& a = *vec;
                const float x = a[s3], y = a[s3 + 1], z = a[s3 + 2];
                // a = q x v (cz, cy, cx = a.x, a.y, a.z in the decompiled names' order)
                const float cz = z * qy - y * qz, cy = x * qz - z * qx, cx = y * qx - x * qy;
                a[d3] = cz * ww + x + (cx * qy - cy * qz) * 2.0f;
                a[d3 + 1] = y + cy * ww + (cz * qz - cx * qx) * 2.0f;
                a[d3 + 2] = z + cx * ww + (cy * qx - cz * qy) * 2.0f;
            }
            auto copy = [&](std::vector<float>& a, size_t comps) {
                if (a.empty()) return;
                for (size_t c = 0; c < comps; ++c) a[(static_cast<size_t>(k) * n + i) * comps + c] = a[i * comps + c];
            };
            for (auto& uv : v.uv) copy(uv, 2);
            copy(v.color, 4);
            copy(v.weights, 4);
            copy(v.joints, 4);
        }
    }
    v.count = total;
    const size_t N = m.indices.size();
    m.indices.resize(N * (I + 1));
    for (const SubMesh& sm : m.subMeshes)
        for (uint32_t k = 1; k <= I; ++k)
            for (uint32_t j = 0; j < sm.indexCount; ++j) {
                size_t dst = sm.indexStart + N * k + j;
                if (dst < m.indices.size() && sm.indexStart + j < N)
                    m.indices[dst] = static_cast<uint16_t>(m.indices[sm.indexStart + j] + n * k);
            }
    for (SubMesh& sm : m.subMeshes) {
        sm.indexStart *= I + 1;
        sm.indexCount *= I + 1;
    }
}

Mesh readMesh(Reader& r, uint32_t version, bool decode) {  // ModelResource::readMesh @0x180145d40
    Mesh m;
    m.u11c = r.u32();
    m.u120 = r.u32();
    m.nodes = r.vec<uint16_t>(r.u16());
    if (version > 0x20029) {
        m.instanceCount = r.u32();
        m.instances = r.vec<float>(10ull * m.instanceCount);  // Transform: scale, rotation, position
    }
    if (version < 0x2002B) throw FormatError("uncompressed vertex streams not supported");
    uint32_t vsize = r.u32();
    const uint8_t* vblob = r.take(vsize);
    if (decode) m.vertices = decodeVertices(vblob, vsize);
    m.boneGroup = r.u16();
    uint16_t nm = r.u16();
    for (uint16_t i = 0; i < nm; ++i) {
        float a[16];
        r.array(a, 16);
        m.skinMatrices.push_back(Mat4::fromArray(a));
    }
    if (version < 0x2002C) throw FormatError("uncompressed indices not supported");
    uint32_t isize = r.u32();
    const uint8_t* iblob = r.take(isize);
    if (decode) m.indices = decodeIndices(iblob, isize, &m.indexBits);
    if (r.u32()) {
        uint32_t type = r.u32();
        if (type != 0) throw FormatError("surface type " + std::to_string(type) + " not supported");
        m.subdiv = readSubdiv(r);
    }
    uint16_t ns = r.u16();
    for (uint16_t i = 0; i < ns; ++i) {
        SubMesh s;
        s.material = r.u32();
        s.indexStart = r.u32();
        s.indexCount = r.u32();
        m.subMeshes.push_back(s);
    }
    if (decode && m.instanceCount) fixupMeshInstances(m);
    return m;
}

}  // namespace

int ModelResource::findNode(const std::string& name) const {
    for (size_t i = 0; i < nodeNames.size(); ++i)
        if (nodeNames[i] == name) return static_cast<int>(i);
    return -1;
}

std::unique_ptr<ModelResource> loadModel(const std::vector<uint8_t>& data, bool decodeGeometry) {
    Reader r(data);
    if (r.u32() != kMoxieMagic || r.u32() != kTypeModel) throw FormatError("not a Moxie model");
    auto m = std::make_unique<ModelResource>();
    m->version = r.u32();
    if (m->version < 0x20029 || m->version > 0x2002E) throw FormatError("model version out of range");
    if (m->version > 0x2002C) {
        m->flags = r.u32();
        if (m->flags & 0x1FF) {
            m->baseModel = r.string();
            if (m->flags & 1) {
                m->overridesMaterials = true;
                uint16_t n = r.u16();
                for (uint16_t i = 0; i < n; ++i) m->materials.push_back(readMaterial(r));
            }
            return m;
        }
    }
    uint16_t n = r.u16();
    for (uint16_t i = 0; i < n; ++i) m->materials.push_back(readMaterial(r));
    n = r.u16();
    for (uint16_t i = 0; i < n; ++i) {
        float a[16];
        r.array(a, 16);
        m->localXforms.push_back(Mat4::fromArray(a));
    }
    if (n) {
        for (uint16_t i = 0; i < n; ++i) m->nodeNames.push_back(r.string());
        for (uint16_t i = 0; i < n; ++i) m->nodeProps.push_back(r.string());
    }
    n = r.u16();
    for (uint16_t i = 0; i < n; ++i) {
        float a[16];
        r.array(a, 16);
        m->bindXforms.push_back(Mat4::fromArray(a));
    }
    m->parents = r.vec<int16_t>(r.u16());
    n = r.u16();
    for (uint16_t i = 0; i < n; ++i) m->meshes.push_back(readMesh(r, m->version, decodeGeometry));
    n = r.u16();
    for (uint16_t i = 0; i < n; ++i) m->boneGroups.push_back(r.vec<uint16_t>(r.u16()));
    if (!r.eof()) throw FormatError("trailing bytes in model");
    return m;
}

}  // namespace oyster
