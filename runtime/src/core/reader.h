// Little-endian binary reader for Moxie containers (packed, unaligned fields).
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace oyster {

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : d_(data), n_(size) {}
    explicit Reader(const std::vector<uint8_t>& v) : d_(v.data()), n_(v.size()) {}

    size_t pos() const { return p_; }
    size_t size() const { return n_; }
    bool eof() const { return p_ >= n_; }
    const uint8_t* data() const { return d_; }

    const uint8_t* take(size_t k) {
        if (k > n_ - p_ || p_ > n_)
            throw FormatError("read of " + std::to_string(k) + " bytes at " + std::to_string(p_) +
                              " beyond " + std::to_string(n_));
        const uint8_t* r = d_ + p_;
        p_ += k;
        return r;
    }
    void skip(size_t k) { take(k); }

    template <typename T> T get() {
        T v;
        std::memcpy(&v, take(sizeof(T)), sizeof(T));
        return v;
    }
    uint8_t u8() { return get<uint8_t>(); }
    uint16_t u16() { return get<uint16_t>(); }
    int16_t s16() { return get<int16_t>(); }
    uint32_t u32() { return get<uint32_t>(); }
    int32_t s32() { return get<int32_t>(); }
    float f32() { return get<float>(); }

    template <typename T> void array(T* out, size_t count) {
        std::memcpy(out, take(sizeof(T) * count), sizeof(T) * count);
    }
    template <typename T> std::vector<T> vec(size_t count) {
        std::vector<T> v(count);
        if (count) array(v.data(), count);
        return v;
    }

    // MOXIE::String::read: u32 length + bytes (no terminator)
    std::string string() {
        uint32_t len = u32();
        const uint8_t* s = take(len);
        return std::string(reinterpret_cast<const char*>(s), len);
    }

private:
    const uint8_t* d_;
    size_t n_;
    size_t p_ = 0;
};

// MOXIE::BitStream::read: LSB-first bit reader. Width 0 reads return 0.
class BitReader {
public:
    BitReader(const uint8_t* data, size_t bytes) : d_(data), bits_(bytes * 8) {}
    BitReader() = default;

    uint32_t read(uint64_t pos, uint32_t width) const {
        if (width == 0) return 0;
        if (width > 32 || pos + width > bits_)
            throw FormatError("bit read " + std::to_string(pos) + "+" + std::to_string(width) +
                              " beyond " + std::to_string(bits_));
        uint64_t byte = pos >> 3;
        uint32_t shift = static_cast<uint32_t>(pos & 7);
        uint64_t v = 0;
        size_t need = (shift + width + 7) >> 3;
        for (size_t i = 0; i < need; ++i) v |= static_cast<uint64_t>(d_[byte + i]) << (8 * i);
        v >>= shift;
        return static_cast<uint32_t>(v & ((width == 32) ? 0xFFFFFFFFull : ((1ull << width) - 1)));
    }
    uint64_t bits() const { return bits_; }

private:
    const uint8_t* d_ = nullptr;
    uint64_t bits_ = 0;
};

inline uint32_t fnv1a(const char* s) {  // MOXIE::hashstr / String::hash
    uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) h = (h ^ static_cast<uint32_t>(static_cast<int>(static_cast<signed char>(*s)))) * 0x1000193u;
    return h;
}
inline uint32_t fnv1a(const std::string& s) { return fnv1a(s.c_str()); }

}  // namespace oyster
