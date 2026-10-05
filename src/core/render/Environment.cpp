#include "render/Environment.h"

#include <cmath>

namespace cad::rt {

namespace {

constexpr float kPi = 3.14159265358979f;

float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

// A GGX-distributed half vector around n.
V3 importanceGGX(float u1, float u2, const V3 &n, float alpha) {
    const float phi = 2.0f * kPi * u1;
    const float cosT = std::sqrt((1.0f - u2) / (1.0f + (alpha * alpha - 1.0f) * u2));
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    V3 t, b;
    basis(n, t, b);
    return normalize(t * (sinT * std::cos(phi)) + b * (sinT * std::sin(phi)) + n * cosT);
}

V3 dirOf(float u, float v) {
    const float phi = (u - 0.5f) * 2.0f * kPi, theta = v * kPi;
    return {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
}

// A soft-edged rectangular panel in (azimuth, elevation), degrees.
float panel(float az, float el, float az0, float az1, float el0, float el1, float soft) {
    auto edge = [&](float x, float a, float b) { return smoothstepf(a - soft, a + soft, x) * (1.0f - smoothstepf(b - soft, b + soft, x)); };
    return edge(az, az0, az1) * edge(el, el0, el1);
}

} // namespace

V3 KeyLight::irradiance() const {
    const float solid = 2.0f * kPi * (1.0f - std::cos(angularRadius));
    return radiance * solid;
}

V3 Environment::radiance(const V3 &d) const {
    const float az = std::atan2(d.y, d.x) * 180.0f / kPi;
    const float el = std::asin(clampf(d.z, -1.0f, 1.0f)) * 180.0f / kPi;
    if(kind == Lighting::Daylight) {
        if(d.z < 0.0f) {
            // Ground: a light stone / concrete, lit by the sun and sky.
            const float g = 0.28f + 0.06f * std::exp(d.z * 4.0f);
            return V3(1.0f, 0.93f, 0.84f) * g;
        }
        const float h = std::pow(1.0f - d.z, 3.0f);
        V3 sky = mix(V3(0.20f, 0.38f, 0.85f), V3(0.78f, 0.86f, 0.96f), h) * 1.15f;
        const float sunDot = std::max(dot(d, key.dir), 0.0f);
        sky += V3(1.0f, 0.86f, 0.62f) * (0.7f * std::pow(sunDot, 12.0f) + 2.5f * std::pow(sunDot, 200.0f));
        return sky;
    }
    // Studio: a seamless backdrop, light from above.
    V3 c;
    if(d.z >= 0.0f) c = V3(1.0f, 0.985f, 0.96f) * (0.30f + 0.22f * d.z);
    else c = V3(0.92f, 0.9f, 0.87f) * (0.10f + 0.12f * (1.0f + d.z));
    // Strip light on the right.
    c += V3(1.0f, 1.0f, 1.0f) * (2.4f * panel(az, el, -16.0f, 8.0f, 4.0f, 58.0f, 2.0f));
    // Dim fill panel at the back left.
    c += V3(0.96f, 0.98f, 1.0f) * (0.75f * panel(az, el, 120.0f, 165.0f, 8.0f, 48.0f, 4.0f));
    // Overhead scrim.
    c += V3(1.0f, 1.0f, 1.0f) * (0.9f * smoothstepf(0.88f, 0.96f, d.z));
    return c;
}

V3 Environment::radianceWithKey(const V3 &d) const {
    V3 c = radiance(d);
    const float ang = std::acos(clampf(dot(d, key.dir), -1.0f, 1.0f));
    // A softbox with a soft rim (the sun with its disk).
    const float soft = key.angularRadius * (kind == Lighting::Daylight ? 0.15f : 0.25f);
    c += key.radiance * (1.0f - smoothstepf(key.angularRadius - soft, key.angularRadius + soft, ang));
    return c;
}

Environment environment(Lighting kind) {
    Environment e;
    e.kind = kind;
    if(kind == Lighting::Daylight) {
        e.key.dir = normalize(V3(-0.55f, 0.15f, 0.82f)); // from the back left: shadows come forward
        e.key.angularRadius = 0.03f; // the sun, widened a little so its reflection can be seen
        const V3 irr = V3(1.0f, 0.94f, 0.86f) * 3.1f;
        e.key.radiance = irr / (2.0f * kPi * (1.0f - std::cos(e.key.angularRadius)));
    } else {
        // A large softbox above and in front, to the left.
        e.key.dir = normalize(V3(-0.42f, -0.55f, 0.72f));
        e.key.angularRadius = 0.17f;
        const V3 irr = V3(1.0f, 0.99f, 0.97f) * 2.7f;
        e.key.radiance = irr / (2.0f * kPi * (1.0f - std::cos(e.key.angularRadius)));
    }
    return e;
}

V3 shIrradiance(const std::array<std::array<float, 3>, 9> &sh, const V3 &n) {
    const float b[9] = {0.282095f,
                        0.488603f * n.y,
                        0.488603f * n.z,
                        0.488603f * n.x,
                        1.092548f * n.x * n.y,
                        1.092548f * n.y * n.z,
                        0.315392f * (3.0f * n.z * n.z - 1.0f),
                        1.092548f * n.x * n.z,
                        0.546274f * (n.x * n.x - n.y * n.y)};
    V3 out;
    for(int i = 0; i < 9; ++i) out += V3(sh[size_t(i)][0], sh[size_t(i)][1], sh[size_t(i)][2]) * b[i];
    return V3(std::max(out.x, 0.0f), std::max(out.y, 0.0f), std::max(out.z, 0.0f));
}

EnvMaps bakeEnvMaps(const Environment &env, int base, int levels, bool includeKey) {
    EnvMaps m;
    m.base = base;
    m.levels = levels;
    m.rgb.resize(size_t(levels));
    for(int lv = 0; lv < levels; ++lv) {
        const int w = std::max(base >> lv, 8), h = std::max(w / 2, 4);
        auto &img = m.rgb[size_t(lv)];
        img.assign(size_t(w * h * 3), 0.0f);
        const float rough = float(lv) / float(std::max(levels - 1, 1));
        const float alpha = std::max(rough * rough, 0.002f);
        const int samples = lv == 0 ? 4 : 64;
        for(int y = 0; y < h; ++y)
            for(int x = 0; x < w; ++x) {
                V3 sum;
                float wsum = 0.0f;
                if(lv == 0) {
                    for(int s = 0; s < samples; ++s) {
                        const V3 d = dirOf((float(x) + 0.25f + 0.5f * float(s & 1)) / float(w),
                                           (float(y) + 0.25f + 0.5f * float(s >> 1)) / float(h));
                        sum += includeKey ? env.radianceWithKey(d) : env.radiance(d);
                        wsum += 1.0f;
                    }
                } else {
                    const V3 n = dirOf((float(x) + 0.5f) / float(w), (float(y) + 0.5f) / float(h));
                    for(int s = 0; s < samples; ++s) {
                        const V3 hv = importanceGGX(float(s) / float(samples), radicalInverse(uint32_t(s)), n, alpha);
                        const V3 l = normalize(hv * (2.0f * dot(n, hv)) - n);
                        const float nl = dot(n, l);
                        if(nl <= 0.0f) continue;
                        sum += (includeKey ? env.radianceWithKey(l) : env.radiance(l)) * nl;
                        wsum += nl;
                    }
                }
                const V3 c = wsum > 0.0f ? sum / wsum : V3();
                const size_t i = size_t((y * w + x) * 3);
                img[i] = c.x, img[i + 1] = c.y, img[i + 2] = c.z;
            }
    }
    // Irradiance SH (without the key light, which is lit directly).
    std::array<std::array<float, 3>, 9> coeff{};
    const int W = 96, H = 48;
    for(int y = 0; y < H; ++y)
        for(int x = 0; x < W; ++x) {
            const float v = (float(y) + 0.5f) / float(H);
            const V3 d = dirOf((float(x) + 0.5f) / float(W), v);
            const float dOmega = (2.0f * kPi / float(W)) * (kPi / float(H)) * std::sin(v * kPi);
            const V3 L = env.radiance(d);
            const float b[9] = {0.282095f,         0.488603f * d.y, 0.488603f * d.z,
                                0.488603f * d.x,   1.092548f * d.x * d.y, 1.092548f * d.y * d.z,
                                0.315392f * (3.0f * d.z * d.z - 1.0f), 1.092548f * d.x * d.z,
                                0.546274f * (d.x * d.x - d.y * d.y)};
            for(int i = 0; i < 9; ++i)
                for(int c = 0; c < 3; ++c) coeff[size_t(i)][size_t(c)] += L[c] * b[i] * dOmega;
        }
    // Cosine convolution, then / pi (radiance off a white Lambert surface).
    const float A[9] = {kPi, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, 2.0f * kPi / 3.0f, kPi / 4.0f,
                        kPi / 4.0f, kPi / 4.0f, kPi / 4.0f, kPi / 4.0f};
    for(int i = 0; i < 9; ++i)
        for(int c = 0; c < 3; ++c) m.sh[size_t(i)][size_t(c)] = coeff[size_t(i)][size_t(c)] * A[i] / kPi;
    return m;
}

float ggxD(float NoH, float alpha) {
    const float a2 = alpha * alpha;
    const float d = NoH * NoH * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

float smithG1(float NoX, float alpha) {
    const float a2 = alpha * alpha;
    return 2.0f * NoX / (NoX + std::sqrt(a2 + (1.0f - a2) * NoX * NoX));
}

std::vector<float> brdfLut(int size) {
    std::vector<float> out(size_t(size * size * 2));
    const int samples = 128;
    for(int j = 0; j < size; ++j)
        for(int i = 0; i < size; ++i) {
            const float NoV = (float(i) + 0.5f) / float(size);
            const float rough = (float(j) + 0.5f) / float(size);
            const float alpha = std::max(rough * rough, 0.002f);
            const V3 V(std::sqrt(1.0f - NoV * NoV), 0.0f, NoV), N(0, 0, 1);
            float A = 0.0f, B = 0.0f;
            for(int s = 0; s < samples; ++s) {
                const V3 H = importanceGGX(float(s) / float(samples), radicalInverse(uint32_t(s)), N, alpha);
                const V3 L = normalize(H * (2.0f * dot(V, H)) - V);
                const float NoL = std::max(L.z, 0.0f), NoH = std::max(H.z, 0.0f), VoH = std::max(dot(V, H), 0.0f);
                if(NoL <= 0.0f) continue;
                const float G = smithG1(NoV, alpha) * smithG1(NoL, alpha);
                const float Gv = G * VoH / std::max(NoH * NoV, 1e-6f);
                const float Fc = std::pow(1.0f - VoH, 5.0f);
                A += (1.0f - Fc) * Gv;
                B += Fc * Gv;
            }
            out[size_t((j * size + i) * 2)] = A / float(samples);
            out[size_t((j * size + i) * 2 + 1)] = B / float(samples);
        }
    return out;
}

} // namespace cad::rt
