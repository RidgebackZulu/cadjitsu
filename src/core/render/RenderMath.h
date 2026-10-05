#pragma once

// Small float vector maths and hash noise for the renderers. The noise and the
// surface models built on it have a line-for-line GLSL twin
// (src/app/shaders/printsurface.glsl), so the live preview and the path
// tracer show the same layer lines and plate grain.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cad::rt {

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;
    constexpr V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3 operator+(const V3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3 operator/(float s) const { return {x / s, y / s, z / s}; }
    V3 operator*(const V3 &o) const { return {x * o.x, y * o.y, z * o.z}; }
    V3 &operator+=(const V3 &o) { x += o.x, y += o.y, z += o.z; return *this; }
    V3 &operator*=(float s) { x *= s, y *= s, z *= s; return *this; }
    V3 &operator*=(const V3 &o) { x *= o.x, y *= o.y, z *= o.z; return *this; }
    float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
};

inline V3 operator*(float s, const V3 &v) { return v * s; }
inline float dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3 &a, const V3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const V3 &v) { return std::sqrt(dot(v, v)); }
inline V3 normalize(const V3 &v) {
    const float l = length(v);
    return l > 1e-20f ? v / l : V3(0, 0, 1);
}
inline float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline V3 mix(const V3 &a, const V3 &b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float e0, float e1, float x) {
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float fractf(float x) { return x - std::floor(x); }
inline float maxc(const V3 &v) { return std::max(v.x, std::max(v.y, v.z)); }

// An orthonormal basis around n (Duff et al. 2017).
inline void basis(const V3 &n, V3 &t, V3 &b) {
    const float s = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z);
    const float c = n.x * n.y * a;
    t = {1.0f + s * n.x * n.x * a, s * c, -s * n.x};
    b = {c, s + n.y * n.y * a, -n.y};
}

// --- hashing and noise (GLSL twin: printsurface.glsl) -------------------------------

// PCG-style integer hash of 3 integers to [0, 1).
inline float hash3(int32_t x, int32_t y, int32_t z) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu;
    h = h * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return float(h & 0x00ffffffu) / 16777216.0f;
}

// Value noise in [0, 1], smooth (C1).
inline float valueNoise(V3 p) {
    const float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    const int ix = int(fx), iy = int(fy), iz = int(fz);
    float ux = p.x - fx, uy = p.y - fy, uz = p.z - fz;
    ux = ux * ux * (3 - 2 * ux), uy = uy * uy * (3 - 2 * uy), uz = uz * uz * (3 - 2 * uz);
    auto h = [&](int a, int b, int c) { return hash3(ix + a, iy + b, iz + c); };
    const float x00 = mixf(h(0, 0, 0), h(1, 0, 0), ux), x10 = mixf(h(0, 1, 0), h(1, 1, 0), ux);
    const float x01 = mixf(h(0, 0, 1), h(1, 0, 1), ux), x11 = mixf(h(0, 1, 1), h(1, 1, 1), ux);
    return mixf(mixf(x00, x10, uy), mixf(x01, x11, uy), uz);
}

// Cellular (Worley) noise in 2D: distance to the nearest feature point (cells
// of size 1), a random id of that cell in [0, 1), and the offset from the
// point to the feature (dx, dy) - the distance's gradient is -(dx, dy) / d.
inline float cellular(float px, float py, float &cellId, float &dxOut, float &dyOut) {
    const float fx = std::floor(px), fy = std::floor(py);
    float best = 8.0f;
    cellId = 0.0f;
    dxOut = dyOut = 0.0f;
    for(int j = -1; j <= 1; ++j)
        for(int i = -1; i <= 1; ++i) {
            const int cx = int(fx) + i, cy = int(fy) + j;
            const float ox = hash3(cx, cy, 11), oy = hash3(cx, cy, 23);
            const float dx = float(cx) + ox - px, dy = float(cy) + oy - py;
            const float d = dx * dx + dy * dy;
            if(d < best) {
                best = d;
                cellId = hash3(cx, cy, 37);
                dxOut = dx;
                dyOut = dy;
            }
        }
    return std::sqrt(best);
}
inline float cellular(float px, float py, float &cellId) {
    float dx, dy;
    return cellular(px, py, cellId, dx, dy);
}

} // namespace cad::rt
