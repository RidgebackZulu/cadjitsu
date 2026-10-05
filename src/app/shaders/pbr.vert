#version 440
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out float vDepth;

#include "pbr_blocks.glsl"

// Bodies in the Rendered style, and in its shadow, thickness and reflection
// passes (each with its own Frame: the key light's, or the mirrored camera's).
void main() {
    vec4 wp = model * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(model) * normal;
    gl_Position = viewProj * wp;
    vDepth = gl_Position.z / gl_Position.w; // orthographic in the shadow passes: exact when interpolated
}
