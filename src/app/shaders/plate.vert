#version 440
#extension GL_GOOGLE_include_directive : enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in float part;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) flat out float vPart;

#include "pbr_blocks.glsl"

// The build plate (already in world coordinates).
void main() {
    vWorldPos = position;
    vNormal = normal;
    vPart = part;
    gl_Position = viewProj * vec4(position, 1.0);
}
