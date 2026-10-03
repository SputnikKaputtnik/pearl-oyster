#include "assets/anim.h"

#include <algorithm>

namespace oyster {

namespace {

AnimChannel readChannel(Reader& r) {
    AnimChannel c;
    c.attrHash = r.u32();
    c.valueType = r.u32();
    uint32_t n = r.u32();
    c.keys.resize(n);
    for (auto& k : c.keys) {
        k.frame = r.u32();
        k.offset = r.u32();
        k.stepBits = r.u32();
    }
    return c;
}

AnimTrack readTrack(Reader& r) {
    AnimTrack t;
    t.name = r.string();
    t.blob = r.vec<uint8_t>(r.u32());
    uint32_t nch = r.u32(), ncust = r.u32();
    for (uint32_t i = 0; i < nch; ++i) t.channels.push_back(readChannel(r));
    for (uint32_t i = 0; i < ncust; ++i) {
        uint32_t a = r.u32();
        uint32_t k = r.u32();
        std::vector<uint32_t> vals = r.vec<uint32_t>(k);
        AnimChannel c = readChannel(r);
        c.customA = a;
        c.customVals = std::move(vals);
        t.custom.push_back(std::move(c));
    }
    return t;
}

std::vector<uint8_t> readBits(Reader& r, uint64_t* nbits) {
    uint32_t n = r.u32();
    *nbits = n;
    return r.vec<uint8_t>((static_cast<size_t>(n) + 7) >> 3);
}

std::unique_ptr<VertexAnim> readVertexAnim(Reader& r) {
    const uint8_t* magic = r.take(4);
    if (std::string(reinterpret_cast<const char*>(magic), 4) != "VANM") throw FormatError("VANM magic");
    auto va = std::make_unique<VertexAnim>();
    va->version = r.u32();
    va->u = r.u32();
    va->fps = r.f32();
    uint32_t nm = r.u32();
    for (uint32_t i = 0; i < nm; ++i) {
        VanmMesh m;
        m.name = r.string();
        m.mode = r.u32();
        uint32_t nchunks = r.u32();
        m.frames = r.u32();
        r.array(m.field8, 8);
        uint32_t nblk = r.u32();
        m.visibility = r.vec<uint16_t>(nblk);
        m.bounds = r.vec<float>(6ull * nblk);
        for (uint32_t k = 0; k < nchunks; ++k) {
            VanmChunk c;
            c.a = r.u32();
            c.frames = r.u32();
            c.vertexCount = r.u32();
            uint32_t blocks = (c.frames + 15) >> 4;
            for (uint32_t b = 0; b < blocks; ++b) {
                uint64_t bits;
                c.blockHeaders.push_back(readBits(r, &bits));
                c.blockHeaderBits.push_back(bits);
            }
            c.data = readBits(r, &c.dataBits);
            m.chunks.push_back(std::move(c));
        }
        va->meshes.push_back(std::move(m));
    }
    return va;
}

// sign = top bit (w-1), magnitude below, unit 1/256
float signmagTop(uint32_t v, uint32_t w) {
    if (w == 0) return 0.0f;
    uint32_t mag = v & ((1u << (w - 1)) - 1);
    float f = static_cast<float>(mag) * (1.0f / 256.0f);
    return ((v >> (w - 1)) & 1) ? -f : f;
}

}  // namespace

std::unique_ptr<AnimResource> loadAnim(const std::vector<uint8_t>& data) {
    Reader r(data);
    if (r.u32() != kMoxieMagic || r.u32() != kTypeAnim) throw FormatError("not a Moxie animation");
    auto a = std::make_unique<AnimResource>();
    a->version = r.u32();
    if (a->version != 1 && a->version != 2) throw FormatError("anim version");
    a->frames = r.u32();
    a->fps = r.u32();
    uint32_t nt = r.u32();
    for (uint32_t i = 0; i < nt; ++i) a->tracks.push_back(readTrack(r));
    a->u50 = r.u32();
    uint8_t hasVA = r.u8();
    if (hasVA) {
        if (a->version == 2) r.u32();  // gsa id (unused in Pearl)
        else a->vertexAnim = readVertexAnim(r);
    }
    if (!r.eof()) throw FormatError("trailing bytes in animation");
    return a;
}

void VanmDecoder::parseHeader(uint32_t block) {
    const std::vector<uint8_t>& blob = c_->blockHeaders[block];
    BitReader bs(blob.data(), blob.size());
    uint32_t n = std::min<uint32_t>(16, c_->frames - 16 * block);
    BlockHeader h;
    h.n = n;
    uint32_t b0 = blob.empty() ? 0 : blob[0];
    uint32_t wa = b0 >> 3, wb = b0 & 7;
    uint64_t pos = 8;
    uint32_t w3[3], w9[9];
    for (auto& w : w3) { w = bs.read(pos, wb); pos += wb; }
    for (auto& w : w9) { w = bs.read(pos, wb); pos += wb; }
    h.widths.resize(3 * n);
    for (auto& w : h.widths) { w = bs.read(pos, wb); pos += wb; }
    h.offsets.resize(n);
    for (auto& o : h.offsets) { o = bs.read(pos, wa); pos += wa; }
    h.mn.resize(3 * n);
    h.mx.resize(3 * n);
    h.off.resize(3 * n);
    std::vector<float>* base[3] = {&h.mn, &h.mx, &h.off};
    for (int g = 0; g < 3; ++g)
        for (int c = 0; c < 3; ++c) { (*base[g])[c] = signmagTop(bs.read(pos, w3[g]), w3[g]); pos += w3[g]; }
    for (int k = 0; k < 9; ++k) {
        std::vector<float>& dst = *base[k / 3];
        int c = k % 3;
        for (uint32_t i = 1; i < n; ++i) { dst[3 * i + c] = signmagTop(bs.read(pos, w9[k]), w9[k]); pos += w9[k]; }
    }
    h_ = std::move(h);
}

void VanmDecoder::decodeInto(uint32_t i, bool delta) {
    BitReader bs(c_->data.data(), c_->data.size());
    uint32_t nv = c_->vertexCount;
    uint64_t pos = h_.offsets[i];
    for (int c = 0; c < 3; ++c) {
        uint32_t w = h_.widths[3 * i + c];
        float mn = h_.mn[3 * i + c], mx = h_.mx[3 * i + c], off = h_.off[3 * i + c];
        float scale = (mx - mn) * (1.0f / static_cast<float>(1u << w));
        for (uint32_t v = 0; v < nv; ++v) {
            uint32_t q = bs.read(pos, w);
            pos += w;
            float val = static_cast<float>(q) * scale + mn + off;
            cur_[3 * v + c] = delta ? cur_[3 * v + c] + val : val;
        }
    }
    maxBit_ = std::max(maxBit_, pos);
}

const std::vector<float>& VanmDecoder::frame(uint32_t f) {
    if (c_->frames == 0) return cur_;
    if (f >= c_->frames) f = c_->frames - 1;
    uint32_t block = f >> 4, i = f & 15;
    cur_.resize(3ull * c_->vertexCount);
    if (static_cast<int32_t>(block) != block_ || static_cast<int32_t>(i) < last_) {
        if (static_cast<int32_t>(block) != block_) parseHeader(block);
        block_ = static_cast<int32_t>(block);
        last_ = -1;
    }
    while (last_ < static_cast<int32_t>(i)) {
        ++last_;
        decodeInto(static_cast<uint32_t>(last_), last_ > 0);
    }
    return cur_;
}

}  // namespace oyster
