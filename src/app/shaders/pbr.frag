#version 440
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in float vDepth;
layout(location = 0) out vec4 fragColor;

#include "pbr_blocks.glsl"
#include "pbr_common.glsl"
#include "printsurface.glsl"

// A printed body in the Rendered style: the print's surface (layer lines,
// top skins, the plate's imprint underneath) lit by the studio's key light,
// with soft shadows, and by the environment around it.
//  pass 0: opaque;
//  pass 1: a semitransparent body's transmittance, multiplied into what is behind;
//  pass 2: the light it reflects and scatters, added on top.
void main() {
    if(misc.x > 0.5 && dot(clipPlane.xyz, vWorldPos) + clipPlane.w > 0.0)
        discard;
    int pass = int(m4.y + 0.5);
    vec3 V = viewVector(vWorldPos);
    vec3 ng = normalize(vNormal);
    bool backFace = dot(ng, V) < 0.0;
    if(backFace) {
        if(pass != 0)
            discard;
        // Inside a body, seen through a section cut.
        ng = -ng;
    }
    vec3 dp = fwidth(vWorldPos);
    float footprint = max(max(dp.x, dp.y), dp.z);
    float strength = surface.z * m3.w;
    SurfaceSample ss = printSurface(vWorldPos, ng, surface.x, surface.y, strength, m4.x, surface.w,
                                    int(plateInfo.x + 0.5), footprint);
    vec3 N = ss.normal;
    if(dot(N, V) < 0.02)
        N = normalize(N + V * (0.02 - dot(N, V)));
    vec3 T = ss.tangent;
    vec3 B = normalize(cross(N, T));

    vec3 albedo = m0.rgb;
    if(backFace)
        albedo *= 0.45;
    float ior = m0.w;
    float metal = m1.w;
    float roughAlong = clamp(m2.x + ss.roughnessAdd, 0.04, 1.0);
    float roughAcross = clamp(m2.y + ss.roughnessAdd + ss.acrossAdd, 0.04, 1.0);
    float rough = sqrt(roughAlong * roughAcross);
    float transmission = m2.w;
    float f0d = pow((ior - 1.0) / (ior + 1.0), 2.0);
    // Silk: the flakes' share (metalness) reflects in the filament's colour.
    vec3 F0 = mix(vec3(f0d), m1.rgb, metal);
    vec3 diffAlbedo = albedo * (1.0 - metal);

    float NoV = clamp(dot(N, V), 1e-3, 1.0);
    vec3 L = keyDir.xyz;
    float NoL = clamp(dot(N, L), 0.0, 1.0) * smoothstep(-0.05, 0.1, dot(ng, L));
    vec3 vis = keyVisibility(vWorldPos, ng);
    vec3 H = normalize(L + V);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    // The softbox (or the sun) has a size: widen the lobe by it.
    float widen = 0.5 * keyDir.w;
    float ax = min(roughAlong * roughAlong + widen, 1.0);
    float ay = min(roughAcross * roughAcross + widen, 1.0);
    float D = abs(ax - ay) > 1e-3 ? D_GGXaniso(NoH, dot(T, H), dot(B, H), ax, ay) : D_GGX(NoH, ax);
    float Vis = V_Smith(NoV, max(NoL, 1e-4), sqrt(ax * ay));
    vec3 F = F_Schlick(F0, VoH);
    vec3 keySpec = D * Vis * F;
    float FV = pow(1.0 - NoV, 5.0);
    // Matte fillers and TPU's soft surface scatter back a little at grazing angles.
    float sheenBoost = 1.0 + m2.z * (0.6 * pow(1.0 - NoV, 3.0));
    vec3 keyDiffuse = diffAlbedo / PI * (1.0 - F0) * sheenBoost;

    // The environment: diffuse from the SH, specular from the pre-filtered map.
    vec3 irr = shIrradiance(N) * ss.cavity;
    // Silk: reflections stretch across the layers.
    vec3 R;
    if(roughAcross - roughAlong > 0.05) {
        float aniso = clamp((roughAcross - roughAlong) / max(roughAcross, 1e-3), 0.0, 1.0);
        vec3 aT = cross(B, V);
        vec3 aN = normalize(cross(aT, B));
        R = reflect(-V, normalize(mix(N, aN, 0.7 * aniso)));
    } else {
        R = reflect(-V, N);
    }
    // Reflections below the plate see the plate (darker), not the floor.
    vec2 ab = textureLod(brdfLut, vec2(NoV, rough), 0.0).rg;
    vec3 envSpec = envSample(R, rough) * (F0 * ab.x + ab.y) * mix(1.0, ss.cavity, 0.6);
    // Specular occlusion where the key is shadowed: the environment's brightest
    // part sits around the key light.
    float shade = dot(vis, vec3(0.3333));
    envSpec *= mix(0.55, 1.0, shade);
    vec3 envDiffuse = irr * diffAlbedo * (1.0 - (F0 * ab.x + ab.y)) * sheenBoost;
    vec3 keyIn = keyIrr.rgb * vis * NoL;

    if(pass == 1) {
        // What is behind shows through, dimmed by the colour over the path
        // through the plastic and by haze (light scattered out of the way).
        float t = textureLod(thicknessMap, gl_FragCoord.xy * viewport.zw, 0.0).r;
        if(t <= 0.01)
            t = 2.0;
        t = plasticAlong(t);
        vec3 sigma = m3.rgb + vec3(1.0 / max(m4.w, 0.05));
        float Fv = f0d + (1.0 - f0d) * FV;
        vec3 Td = transmission * (1.0 - Fv) * (1.0 - Fv) * exp(-sigma * t);
        fragColor = vec4(Td, 1.0);
        return;
    }

    vec3 c;
    if(pass == 2) {
        float t = textureLod(thicknessMap, gl_FragCoord.xy * viewport.zw, 0.0).r;
        if(t <= 0.01)
            t = 2.0;
        t = plasticAlong(t);
        float Fv = f0d + (1.0 - f0d) * FV;
        // The share of light scattered inside on its way through: it comes
        // out diffuse, in the plastic's colour, lit from all around (and
        // from behind, by the key light passing through).
        float scattered = 1.0 - exp(-t / max(m4.w, 0.05));
        vec3 tintColor = exp(-m3.rgb * min(t, 2.0 * m4.w));
        vec3 around = shIrradiance(N) + 0.6 * shIrradiance(-N);
        float wrapL = clamp((dot(N, L) + 0.6) / 1.6, 0.0, 1.0);
        float backL = pow(clamp(dot(-V, L), 0.0, 1.0), 2.0) * 0.6;
        vec3 glow = tintColor * (around * 0.5 + keyIrr.rgb * vis * (wrapL + backL) / PI);
        vec3 opaquePart = keyDiffuse * keyIn + envDiffuse;
        c = keySpec * keyIn + envSpec
          + (1.0 - transmission) * opaquePart
          + transmission * (1.0 - Fv) * scattered * glow;
    } else {
        c = (keyDiffuse + keySpec) * keyIn + envDiffuse + envSpec;
    }
    if(misc.z > 0.5) {
        fragColor = vec4(c, 1.0); // the plate's reflection: linear light
        return;
    }
    fragColor = vec4(toDisplay(c), 1.0);
}
