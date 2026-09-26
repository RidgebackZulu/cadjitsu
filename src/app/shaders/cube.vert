#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec3 vPos;

layout(std140, binding = 0) uniform Cube {
    mat4 mvp;
    mat4 rotation;   // cube (world) space to view space
    vec4 highlight;  // xyz = hovered region (-1 / 0 / 1 per axis), w = 1 if any
};

void main() {
    vNormal = mat3(rotation) * normal;
    vUv = uv;
    vPos = position;
    gl_Position = mvp * vec4(position, 1.0);
}
