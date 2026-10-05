#version 440
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in float vDepth;
layout(location = 0) out vec4 fragColor;

#include "pbr_blocks.glsl"

// Seen from the key light. Opaque bodies (pass 0): the depth of the nearest
// surface. Semitransparent ones (pass 1): the colour of the light they let
// through (multiplied in), and the depth of the nearest (kept by a Min blend).
void main() {
    if(m4.y < 0.5) {
        fragColor = vec4(vDepth, vDepth, vDepth, 1.0);
        return;
    }
    // Each body darkens the light once: on its faces towards the light.
    if(dot(vNormal, keyDir.xyz) < 0.0)
        discard;
    vec3 through = exp(-m3.rgb * m4.z);
    float f0 = pow((m0.w - 1.0) / (m0.w + 1.0), 2.0);
    vec3 tint = mix(vec3(0.0), through * (1.0 - f0) * (1.0 - f0), m2.w);
    // Light scattered inside still comes out, mostly forwards.
    tint = max(tint, m2.w * 0.35 * through);
    fragColor = vec4(tint, vDepth);
}
