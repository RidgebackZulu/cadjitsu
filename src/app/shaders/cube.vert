#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 region;
layout(location = 4) in float material;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec3 vPos;
layout(location = 3) out vec3 vObjNormal;
layout(location = 4) flat out vec3 vRegion;
layout(location = 5) flat out float vMaterial;

layout(std140, binding = 0) uniform Cube {
    mat4 mvp;
    mat4 rotation;   // cube (world) space to view space
    vec4 highlight;  // xyz = hovered region (-1 / 0 / 1 per axis), w = 1 if any
};

void main() {
    vNormal = mat3(rotation) * normal;
    vObjNormal = normal;
    vUv = uv;
    vPos = position;
    vRegion = region;
    vMaterial = material;
    gl_Position = mvp * vec4(position, 1.0);
}
