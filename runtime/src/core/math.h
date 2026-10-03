// Single-precision math matching the engine's conventions:
//  * Matrix4 is stored row-major with the translation in elements 3, 7, 11 (as in .mxm files)
//    and transforms column vectors: p' = M * p. Hierarchies compose as world = parent * local.
//  * Shaders compute `position * u_matrix`; uploading the row-major array unchanged
//    (transpose = GL_FALSE) makes GLSL see the transpose, so both agree.
//  * Quaternion component order x, y, z, w.
#pragma once
#include <cmath>
#include <cstring>

namespace oyster {

struct Vec2 { float x = 0, y = 0; };

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
    Vec3 normalized() const { float l = length(); return l > 0 ? *this * (1.0f / l) : *this; }
};

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    Quat(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    Quat operator*(const Quat& q) const {
        return {w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z};
    }
    float dot(const Quat& q) const { return x * q.x + y * q.y + z * q.z + w * q.w; }
    Quat normalized() const {
        float l = std::sqrt(dot(*this));
        return l > 0 ? Quat{x / l, y / l, z / l, w / l} : *this;
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    Vec3 rotate(const Vec3& v) const {
        Quat p{v.x, v.y, v.z, 0};
        Quat r = (*this) * p * conjugate();
        return {r.x, r.y, r.z};
    }
};

struct Mat4 {
    float m[16];  // row-major, m[r*4+c]

    Mat4() { setIdentity(); }
    static Mat4 identity() { return Mat4(); }
    void setIdentity() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
    static Mat4 fromArray(const float* a) { Mat4 r; std::memcpy(r.m, a, sizeof(r.m)); return r; }
    float& at(int r, int c) { return m[r * 4 + c]; }
    float at(int r, int c) const { return m[r * 4 + c]; }

    Mat4 operator*(const Mat4& b) const {
        Mat4 o;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += at(r, k) * b.at(k, c);
                o.at(r, c) = s;
            }
        return o;
    }
    Vec3 transformPoint(const Vec3& p) const {
        return {at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z + at(0, 3),
                at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z + at(1, 3),
                at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z + at(2, 3)};
    }
    Vec3 transformDir(const Vec3& p) const {
        return {at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z, at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z,
                at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z};
    }
    Vec3 translation() const { return {m[3], m[7], m[11]}; }

    Mat4 transposed() const {
        Mat4 o;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) o.at(r, c) = at(c, r);
        return o;
    }

    static Mat4 translation(const Vec3& t) {
        Mat4 o;
        o.m[3] = t.x; o.m[7] = t.y; o.m[11] = t.z;
        return o;
    }
    static Mat4 rotation(const Quat& q) {
        Mat4 o;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        o.at(0, 0) = 1 - 2 * (yy + zz); o.at(0, 1) = 2 * (xy - wz);     o.at(0, 2) = 2 * (xz + wy);
        o.at(1, 0) = 2 * (xy + wz);     o.at(1, 1) = 1 - 2 * (xx + zz); o.at(1, 2) = 2 * (yz - wx);
        o.at(2, 0) = 2 * (xz - wy);     o.at(2, 1) = 2 * (yz + wx);     o.at(2, 2) = 1 - 2 * (xx + yy);
        return o;
    }
    static Mat4 scale(const Vec3& s) {
        Mat4 o;
        o.m[0] = s.x; o.m[5] = s.y; o.m[10] = s.z;
        return o;
    }
    static Mat4 trs(const Vec3& t, const Quat& r, const Vec3& s) {
        Mat4 o = rotation(r);
        for (int row = 0; row < 3; ++row) {
            o.at(row, 0) *= s.x;
            o.at(row, 1) *= s.y;
            o.at(row, 2) *= s.z;
        }
        o.m[3] = t.x; o.m[7] = t.y; o.m[11] = t.z;
        return o;
    }

    Mat4 inverse() const;  // general 4x4 inverse
};

inline Mat4 Mat4::inverse() const {
    const float* a = m;
    float inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    Mat4 o;
    if (det == 0) return o;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) o.m[i] = inv[i] * det;
    return o;
}

}  // namespace oyster
