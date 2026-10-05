#pragma once

#include "render/Materials.h"
#include "render/RenderMath.h"

#include <array>
#include <vector>

namespace cad::rt {

// The light around the scene, shared by the live preview and the path tracer:
//  - Studio: a photographic product set - a large overhead-front softbox
//    (the key light), a strip light at the right, a dim fill at the left,
//    a seamless grey backdrop and a darker floor bounce;
//  - Daylight: a clear sky with the sun.
// The key light (softbox or sun) is also given as an area light so it can
// cast sharp-to-soft shadows; the environment itself holds everything else.
struct KeyLight {
    V3 dir;            // towards the light (world, Z up)
    V3 radiance;       // per steradian, linear
    float angularRadius = 0.1f; // radians (softbox / sun disk size)
    V3 irradiance() const; // at normal incidence
};

struct Environment {
    Lighting kind = Lighting::Studio;
    KeyLight key;
    // Radiance arriving from `dir` (towards the sky), without the key light.
    V3 radiance(const V3 &dir) const;
    // With the key light's shape included (what reflections see).
    V3 radianceWithKey(const V3 &dir) const;
};

Environment environment(Lighting kind);

// Pre-filtered equirect maps for image-based lighting on the GPU: level i is
// convolved with GGX at roughness i / (levels - 1). Row-major RGB floats,
// width = base >> i, height = width / 2; direction (x, y, z) maps to
// u = atan2(y, x) / 2pi + 0.5, v = acos(z) / pi. Without the key light by
// default: the live preview lights with it directly, where shadows can stop it.
struct EnvMaps {
    int base = 256, levels = 6;
    std::vector<std::vector<float>> rgb; // per level
    std::array<std::array<float, 3>, 9> sh{}; // irradiance (diffuse, cosine-convolved) SH, without the key light
};
EnvMaps bakeEnvMaps(const Environment &env, int base = 256, int levels = 6, bool includeKey = false);

// Diffuse irradiance / pi from the SH (what a white Lambert surface reflects).
V3 shIrradiance(const std::array<std::array<float, 3>, 9> &sh, const V3 &n);

// The split-sum BRDF table: (scale, bias) of F0 for GGX/Smith at (N.V, roughness).
std::vector<float> brdfLut(int size = 64);

// Shared microfacet maths (also in the GLSL and the path tracer).
float ggxD(float NoH, float alpha);
float smithG1(float NoX, float alpha);

} // namespace cad::rt
