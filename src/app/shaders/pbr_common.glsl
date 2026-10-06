// Shared by the Rendered style's fragment shaders (after pbr_blocks.glsl):
// textures, BRDF, image-based lighting, soft shadows and tone mapping.

layout(binding = 2) uniform sampler2D envMap;
layout(binding = 3) uniform sampler2D brdfLut;
layout(binding = 5) uniform sampler2D shadowMap;
layout(binding = 6) uniform sampler2D shadowTint;
layout(binding = 7) uniform sampler2D reflectionMap;
layout(binding = 8) uniform sampler2D contactMap;
layout(binding = 9) uniform sampler2D thicknessMap;

const float PI = 3.14159265358979;

// Plastic crossed on a straight path of `len` mm through a printed part: two
// walls of 3 perimeters, then infill (twin of cad::plasticAlong).
float plasticAlong(float len) {
    float walls = 6.0 * surface.y;
    return min(len, walls) + flips.y * max(len - walls, 0.0);
}

vec3 viewVector(vec3 p) {
    return eyePos.w > 0.5 ? -eyeDir.xyz : normalize(eyePos.xyz - p);
}

vec3 shIrradiance(vec3 n) {
    vec3 r = sh[0].rgb * 0.282095
           + sh[1].rgb * (0.488603 * n.y) + sh[2].rgb * (0.488603 * n.z) + sh[3].rgb * (0.488603 * n.x)
           + sh[4].rgb * (1.092548 * n.x * n.y) + sh[5].rgb * (1.092548 * n.y * n.z)
           + sh[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0)) + sh[7].rgb * (1.092548 * n.x * n.z)
           + sh[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
    return max(r, vec3(0.0));
}

vec3 envSample(vec3 d, float roughness) {
    float u = atan(d.y, d.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(d.z, -1.0, 1.0)) / PI;
    return textureLod(envMap, vec2(u, v), roughness * plateInfo.w).rgb;
}

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Anisotropic GGX (Burley): ax along the tangent, ay along the bitangent.
float D_GGXaniso(float NoH, float ToH, float BoH, float ax, float ay) {
    float d = ToH * ToH / (ax * ax) + BoH * BoH / (ay * ay) + NoH * NoH;
    return 1.0 / (PI * ax * ay * d * d);
}

float V_Smith(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 F_Schlick(vec3 f0, float VoH) {
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

// Khronos PBR Neutral tone mapping: keeps product colours true (for sRGB displays).
vec3 toneMap(vec3 c) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(c.r, min(c.g, c.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    c -= offset;
    float peak = max(c.r, max(c.g, c.b));
    if(peak >= startCompression) {
        const float d = 1.0 - startCompression;
        float newPeak = 1.0 - d * d / (peak + d - startCompression);
        c *= newPeak / peak;
        float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
        c = mix(c, vec3(newPeak), g);
    }
    return c;
}

vec3 toDisplay(vec3 linearColor) {
    vec3 c = clamp(toneMap(linearColor * keyIrr.w), 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

const vec2 poisson[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
    vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379), vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790));

// Light from the key reaching p: 1 = lit; soft shadows whose penumbra
// widens with the distance to the occluder (as under a real softbox), tinted
// by semitransparent occluders.
vec3 keyVisibility(vec3 p, vec3 ng) {
    if(shadowInfo.w < 0.5)
        return vec3(1.0);
    // Offset along the normal against acne, more at grazing light.
    float slope = clamp(1.0 - dot(ng, keyDir.xyz), 0.0, 1.0);
    vec3 q = p + ng * (shadowInfo.z / 1024.0) * (0.6 + 1.5 * slope);
    vec4 lp = lightViewProj * vec4(q, 1.0);
    vec3 s = lp.xyz / lp.w;
    vec2 uv = s.xy * 0.5 + 0.5;
    if(flips.x > 0.5)
        uv.y = 1.0 - uv.y;
    if(any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return vec3(1.0);
    float recv = s.z;
    if(recv > 0.999)
        return vec3(1.0); // beyond the shadow map's depth: nothing there casts a shadow
    float bias = 0.0008;
    // Blocker search.
    float searchR = 18.0 * shadowInfo.x;
    float blockers = 0.0, count = 0.0;
    for(int i = 0; i < 12; ++i) {
        float d = textureLod(shadowMap, uv + poisson[i] * searchR, 0.0).r;
        if(d < recv - bias) {
            blockers += d;
            count += 1.0;
        }
    }
    float lit = 1.0;
    if(count > 0.0) {
        float avg = blockers / count;
        // Penumbra in world units -> uv.
        float distWorld = (recv - avg) * shadowInfo.y;
        float penumbra = distWorld * keyDir.w / shadowInfo.z;
        float r = max(penumbra, 1.2 * shadowInfo.x);
        float sum = 0.0;
        for(int i = 0; i < 16; ++i) {
            float d = textureLod(shadowMap, uv + poisson[i] * r, 0.0).r;
            sum += d < recv - bias ? 0.0 : 1.0;
        }
        lit = sum / 16.0;
    }
    vec3 vis = vec3(lit);
    vec4 tint = textureLod(shadowTint, uv, 0.0);
    if(recv > tint.a + bias)
        vis *= tint.rgb;
    return vis;
}
