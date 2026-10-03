#include "assets/geometry.h"

#include <cfloat>
#include <cstring>

#include "core/reader.h"

namespace oyster {

namespace {

// Fixed-point sign/magnitude used for attribute ranges: magnitude bits [0,e), sign bit e, unit 1/256.
float signmag(uint32_t v, uint32_t e) {
    uint32_t mag = e >= 32 ? v : (v & ((1u << e) - 1));
    float f = static_cast<float>(mag) * (1.0f / 256.0f);
    return ((v >> e) & 1) ? -f : f;
}

// FUN_1800bbcb0: decodes `count` vertices of an attribute with `ncomp` components into out
// (interleaved, ncomp per vertex). Returns the new bit position.
uint64_t decodeAttribute(const BitReader& bs, uint64_t pos, uint32_t ncomp, uint32_t count, std::vector<float>& out) {
    out.assign(static_cast<size_t>(count) * ncomp, 0.0f);
    uint32_t mode = bs.read(pos, 4);
    pos += 4;
    if (mode & 8) {  // raw floats, component-interleaved
        for (size_t i = 0; i < out.size(); ++i) {
            uint32_t b = bs.read(pos, 32);
            pos += 32;
            std::memcpy(&out[i], &b, 4);
        }
        return pos;
    }
    uint32_t widths[4] = {0, 0, 0, 0};
    for (uint32_t c = 0; c < ncomp; ++c) { widths[c] = bs.read(pos, 5); pos += 5; }
    uint32_t e = bs.read(pos, 5);
    pos += 5;
    if (mode & 4) {  // one constant per component
        float consts[4];
        for (uint32_t c = 0; c < ncomp; ++c) { consts[c] = signmag(bs.read(pos, e + 1), e); pos += e + 1; }
        for (uint32_t c = 0; c < ncomp; ++c) pos += widths[c];  // engine advances by the widths
        for (uint32_t i = 0; i < count; ++i)
            for (uint32_t c = 0; c < ncomp; ++c) out[i * ncomp + c] = consts[c];
        return pos;
    }
    float mins[4], maxs[4];
    for (uint32_t c = 0; c < ncomp; ++c) { mins[c] = signmag(bs.read(pos, e + 1), e); pos += e + 1; }
    for (uint32_t c = 0; c < ncomp; ++c) { maxs[c] = signmag(bs.read(pos, e + 1), e); pos += e + 1; }

    std::vector<uint32_t> sharedIdx;
    std::vector<uint32_t> perIdx[4];
    uint32_t sharedSize = 0, perSize[4] = {0, 0, 0, 0};
    if (mode & 1) {
        uint32_t iw = bs.read(pos, 5);
        sharedSize = bs.read(pos + 5, iw) + 1;
        pos += 5 + iw;
        sharedIdx.resize(count);
        for (uint32_t i = 0; i < count; ++i) { sharedIdx[i] = bs.read(pos, iw); pos += iw; }
    } else if (mode & 2) {
        uint32_t iws[4];
        for (uint32_t c = 0; c < ncomp; ++c) { iws[c] = bs.read(pos, 5); pos += 5; }
        for (uint32_t c = 0; c < ncomp; ++c) {
            if (!widths[c]) continue;
            uint32_t iw = iws[c];
            perSize[c] = bs.read(pos, iw) + 1;
            perIdx[c].resize(count);
            for (uint32_t i = 0; i < count; ++i) perIdx[c][i] = bs.read(pos + iw + static_cast<uint64_t>(i) * iw, iw);
            pos += iw + static_cast<uint64_t>(iw) * count;
        }
    }
    std::vector<float> vals;
    for (uint32_t c = 0; c < ncomp; ++c) {
        uint32_t w = widths[c];
        uint32_t n = count;
        const std::vector<uint32_t>* idx = nullptr;
        if (w) {
            if (mode & 1) { n = sharedSize; idx = &sharedIdx; }
            else if (mode & 2) { n = perSize[c]; idx = &perIdx[c]; }
        }
        float scale = (maxs[c] - mins[c]) * (1.0f / static_cast<float>(1u << w));
        vals.resize(n);
        for (uint32_t k = 0; k < n; ++k) {
            uint32_t q = bs.read(pos, w);
            pos += w;
            vals[k] = static_cast<float>(q) * scale + mins[c];
        }
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t j = idx ? (*idx)[i] : i;
            if (j >= vals.size()) throw FormatError("palette index out of range");
            out[i * ncomp + c] = vals[j];
        }
    }
    return pos;
}

}  // namespace

VertexData decodeVertices(const uint8_t* blob, size_t size) {
    VertexData v;
    BitReader bs(blob, size);
    v.count = bs.read(0, 24);
    if (v.count == 0) { v.bitsUsed = 24; return v; }
    v.flags = bs.read(24, 16);
    uint64_t pos = 40;
    if (v.flags & 1) pos = decodeAttribute(bs, pos, 3, v.count, v.position);
    if (v.flags & 2) pos = decodeAttribute(bs, pos, 3, v.count, v.normal);
    if (v.flags & 4) pos = decodeAttribute(bs, pos, 3, v.count, v.tangent);
    if (v.flags & 0x80) pos = decodeAttribute(bs, pos, 4, v.count, v.color);
    for (int k = 0; k < 4; ++k)
        if (v.flags & (8u << k)) pos = decodeAttribute(bs, pos, 2, v.count, v.uv[k]);
    if (v.flags & 0x100) {
        pos = decodeAttribute(bs, pos, 4, v.count, v.weights);
        for (uint32_t i = 0; i < v.count; ++i) {
            float* w = &v.weights[i * 4];
            float s = w[0] + w[1] + w[2] + w[3];
            if (s < FLT_EPSILON) s = FLT_EPSILON;
            for (int c = 0; c < 4; ++c) w[c] = w[c] / s;
        }
    }
    if (v.flags & 0x200) {
        uint32_t base = bs.read(pos, 8);
        uint32_t width = bs.read(pos + 8, 4);
        pos += 12;
        v.joints.resize(static_cast<size_t>(v.count) * 4);
        for (uint32_t i = 0; i < v.count; ++i)
            for (int c = 0; c < 4; ++c) {
                v.joints[i * 4 + c] = static_cast<float>(bs.read(pos, width) + base);
                pos += width;
            }
    }
    v.bitsUsed = pos;
    return v;
}

std::vector<uint16_t> decodeIndices(const uint8_t* blob, size_t size, uint64_t* bitsUsed) {
    BitReader bs(blob, size);
    std::vector<uint16_t> out;
    uint32_t n = bs.read(0, 32);
    if (n == 0) { if (bitsUsed) *bitsUsed = 32; return out; }
    uint32_t mode = bs.read(32, 2);
    out.resize(n);
    uint64_t pos;
    if (mode == 0) {
        uint32_t width = bs.read(34, 4) + 1;
        uint32_t base = bs.read(38, width);
        pos = 38 + width;
        for (uint32_t i = 0; i < n; ++i) { out[i] = static_cast<uint16_t>((bs.read(pos, width) + base) & 0xFFFF); pos += width; }
    } else if (mode == 1) {
        pos = 34;
        for (uint32_t i = 0; i < n; ++i) { out[i] = static_cast<uint16_t>(bs.read(pos, 16)); pos += 16; }
    } else {
        throw FormatError("index mode " + std::to_string(mode));
    }
    if (bitsUsed) *bitsUsed = pos;
    return out;
}

}  // namespace oyster
