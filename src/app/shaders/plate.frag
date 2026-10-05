#version 440
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) flat in float vPart;
layout(location = 0) out vec4 fragColor;

#include "pbr_blocks.glsl"
#include "pbr_common.glsl"
#include "printsurface.glsl"

const vec2 disk[12] = vec2[](vec2(0.0, 0.0), vec2(0.54, 0.12), vec2(-0.32, 0.47), vec2(-0.41, -0.35),
                             vec2(0.22, -0.52), vec2(0.93, -0.21), vec2(0.62, 0.74), vec2(-0.12, 0.96),
                             vec2(-0.87, 0.36), vec2(-0.79, -0.58), vec2(0.05, -0.99), vec2(0.71, -0.66));

// How much of the model is right above p, blurred over a radius (uv).
float coverage(vec2 uv, float radius, float lod) {
    float s = 0.0;
    for(int i = 0; i < 12; ++i)
        s += textureLod(contactMap, uv + disk[i] * radius, lod).r;
    return s / 12.0;
}

// The build plate: PEI on spring steel on the heated bed, in the key light's
// shadows, darkened where the model stands on it, and reflecting the model
// when it is smooth PEI.
void main() {
    int part = int(vPart + 0.5);
    vec3 p = vWorldPos;
    vec3 ng = normalize(vNormal);
    vec3 V = viewVector(p);
    vec3 dp = fwidth(p);
    float footprint = max(max(dp.x, dp.y), dp.z);
    int kind = int(plateInfo.x + 0.5);
    PlateShade s = plateShade(kind, part, p, ng, footprint);
    vec3 N = s.normal;
    float rough = clamp(s.roughness, 0.03, 1.0);
    vec3 F0 = mix(vec3(0.04), s.albedo, s.metalness);
    vec3 diffAlbedo = s.albedo * (1.0 - s.metalness);
    float NoV = clamp(dot(N, V), 1e-3, 1.0);

    // Ambient occlusion where the model stands on the plate.
    float ao = 1.0;
    if(plateInfo.z > 0.5 && part == 0) {
        vec4 lp = contactViewProj * vec4(p, 1.0);
        vec2 uv = lp.xy / lp.w * 0.5 + 0.5;
        if(flips.x > 0.5)
            uv.y = 1.0 - uv.y;
        float c = 0.5 * coverage(uv, 0.006, 1.5) + 0.5 * coverage(uv, 0.03, 3.5);
        ao = 1.0 - 0.75 * c;
    }

    vec3 L = keyDir.xyz;
    float NoL = clamp(dot(N, L), 0.0, 1.0) * step(0.0, dot(ng, L));
    vec3 vis = keyVisibility(p, ng);
    vec3 H = normalize(L + V);
    float a = min(rough * rough + 0.5 * keyDir.w, 1.0);
    float D = D_GGX(clamp(dot(N, H), 0.0, 1.0), a);
    float Vis = V_Smith(NoV, max(NoL, 1e-4), a);
    vec3 F = F_Schlick(F0, clamp(dot(V, H), 0.0, 1.0));
    vec3 keyIn = keyIrr.rgb * vis * NoL;
    vec3 c = (diffAlbedo / PI * (1.0 - F0) + D * Vis * F) * keyIn;

    vec2 ab = textureLod(brdfLut, vec2(NoV, rough), 0.0).rg;
    vec3 specW = F0 * ab.x + ab.y;
    vec3 env = envSample(reflect(-V, N), rough);
    vec4 refl = vec4(0.0);
    if(plateInfo.y > 0.5 && part == 0) {
        // The model mirrored in the plate (rendered from below it), blurrier
        // where the plate is rougher.
        vec2 uv = gl_FragCoord.xy * viewport.zw + (N.xy - ng.xy) * 0.05;
        refl = textureLod(reflectionMap, uv, rough * 6.0);
        env = mix(env, refl.rgb / max(refl.a, 1e-3), clamp(refl.a, 0.0, 1.0));
    }
    c += shIrradiance(N) * diffAlbedo * (1.0 - specW) * ao + env * specW * mix(1.0, ao, 0.7);
    if(kind == 1 && part == 0) {
        // Smooth PEI is a clear film: a dielectric gloss over the gold, sharp.
        float cc = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);
        vec3 sharp = envSample(reflect(-V, ng), 0.02);
        if(refl.a > 0.0) {
            vec4 r0 = textureLod(reflectionMap, gl_FragCoord.xy * viewport.zw, 0.0);
            sharp = mix(sharp, r0.rgb / max(r0.a, 1e-3), clamp(r0.a, 0.0, 1.0));
        }
        c = c * (1.0 - cc) + sharp * cc * mix(1.0, ao, 0.5);
    }
    fragColor = vec4(toDisplay(c), 1.0);
}
