#include "render/PrintSurface.h"

namespace cad::rt {

float beadProfile(float g, float &slope) {
    // A semi-ellipse across the bead (a squashed round bead), with the slope
    // capped at the seams where two beads meet.
    const float u = 2.0f * clampf(g, 0.0f, 1.0f) - 1.0f;
    const float r = std::sqrt(std::max(1.0f - u * u, 1e-4f));
    slope = clampf(-2.0f * u / r, -3.0f, 3.0f);
    return r;
}

SurfaceSample printSurface(const V3 &p, const V3 &ng, const SurfaceParams &s) {
    SurfaceSample out;
    out.normal = ng;
    const V3 Z(0, 0, 1);
    const float cz = ng.z;
    const float h = std::max(s.layerHeight, 1e-3f), w = std::max(s.lineWidth, 1e-3f);
    // Detail finer than the footprint fades into roughness (no moire).
    const float fade = 1.0f - smoothstepf(0.35f, 1.2f, s.footprint / h);
    out.roughnessAdd = (1.0f - fade) * 0.12f * s.strength;
    const float tz = (p.z - s.plateZ) / h;
    const float layer = std::floor(tz);
    const int k = int(layer);
    float f = tz - layer;
    // Layers differ slightly (extrusion and Z wobble).
    const float jitter = 0.8f + 0.4f * hash3(k, 7, 3);
    const float A = 0.18f * s.strength * jitter * fade; // bead bulge, in layer heights

    V3 n = ng, tangent;
    float cavity = 1.0f;
    if(cz < -0.98f && std::abs(p.z - s.plateZ) < 0.6f * h) {
        // The first layer's underside: a copy of the plate.
        if(s.plateKind == 0) {
            const float e = 0.02f;
            const V3 q = p * 5.0f;
            const float gx = valueNoise(q + V3(e, 0, 0)) - valueNoise(q - V3(e, 0, 0));
            const float gy = valueNoise(q + V3(0, e, 0)) - valueNoise(q - V3(0, e, 0));
            n = normalize(ng + V3(gx, gy, 0) * (0.35f / (2 * e)) * 0.05f * fade);
            out.roughnessAdd += 0.25f;
        }
        tangent = V3(1, 0, 0);
    } else if(std::abs(cz) > 0.98f) {
        // Top skin (or a bridged underside): lines across the face, turning 90 degrees each layer.
        const bool up = cz > 0.0f;
        const int topLayer = up ? int(std::floor(tz - 0.5f)) : k;
        const V3 dir = (topLayer & 1) ? V3(0.70710678f, -0.70710678f, 0) : V3(0.70710678f, 0.70710678f, 0);
        const V3 across(-dir.y, dir.x, 0);
        const float v = dot(p, across) / w;
        float slope;
        const float g = v - std::floor(v);
        const float bump = beadProfile(g, slope);
        const float amp = (up ? 0.05f : 0.14f) * s.strength * fade * h / w; // skins are flatter than walls
        n = normalize(ng - across * (slope * amp * (up ? 1.0f : -1.0f)));
        cavity = 1.0f - (up ? 0.06f : 0.12f) * s.strength * fade * (1.0f - bump);
        tangent = dir;
        if(!up) out.roughnessAdd += 0.12f; // bridges sag and are rough
    } else {
        // Walls and slopes: risers (bead sides) and, on slopes, flat treads.
        V3 nh(ng.x, ng.y, 0);
        nh = normalize(nh);
        tangent = normalize(cross(Z, nh));
        const float q = std::min(cz * cz, 0.92f); // tread share of the period (cos^2 of the slope)
        float riser = 1.0f;
        V3 tread = cz > 0.0f ? Z : -Z;
        if(q > 0.01f) {
            const float edge = 0.06f;
            riser = smoothstepf(q - edge, q + edge, f);
            f = clampf((f - q) / (1.0f - q), 0.0f, 1.0f);
        }
        float slope;
        const float bump = beadProfile(f, slope);
        // The bead's side bulges out by A * h * bump; along z its slope tilts the normal.
        const V3 riserN = normalize(nh - Z * (slope * A / std::max(1.0f - q, 0.08f)));
        n = normalize(mix(tread, riserN, riser));
        // Blend toward the geometric normal as detail fades.
        n = normalize(mix(ng, n, fade));
        cavity = 1.0f - 0.22f * s.strength * fade * riser * (1.0f - bump);
    }
    // Fine grain.
    if(s.micro > 0.0f) {
        const float e = 0.01f;
        const V3 q = p * 22.0f;
        const V3 grad(valueNoise(q + V3(e, 0, 0)) - valueNoise(q - V3(e, 0, 0)),
                      valueNoise(q + V3(0, e, 0)) - valueNoise(q - V3(0, e, 0)),
                      valueNoise(q + V3(0, 0, e)) - valueNoise(q - V3(0, 0, e)));
        V3 g = grad * (1.0f / (2.0f * e)) * (0.06f * s.micro);
        g = g - n * dot(g, n);
        n = normalize(n + g);
    }
    out.normal = n;
    out.tangent = normalize(tangent - out.normal * dot(tangent, out.normal));
    out.cavity = clampf(cavity, 0.0f, 1.0f);
    return out;
}

} // namespace cad::rt
