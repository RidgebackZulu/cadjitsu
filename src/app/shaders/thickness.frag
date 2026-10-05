#version 440
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in float vDepth;
layout(location = 0) out vec4 fragColor;

#include "pbr_blocks.glsl"

// How much semitransparent material each view ray crosses: added up over
// every surface, minus where it enters, plus where it leaves.
void main() {
    if(misc.x > 0.5 && dot(clipPlane.xyz, vWorldPos) + clipPlane.w > 0.0)
        discard;
    float d = eyePos.w > 0.5 ? dot(vWorldPos - eyePos.xyz, eyeDir.xyz) : length(vWorldPos - eyePos.xyz);
    vec3 V = eyePos.w > 0.5 ? -eyeDir.xyz : normalize(eyePos.xyz - vWorldPos);
    float s = dot(vNormal, V) > 0.0 ? -1.0 : 1.0;
    fragColor = vec4(s * (d - params.x), 0.0, 0.0, 0.0);
}
