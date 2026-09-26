#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 vWorldPos;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 eyePos;
    vec4 eyeDir;
    vec4 lightDir;
    vec4 viewport;
    vec4 clipPlane;
    vec4 misc;
};

layout(std140, binding = 1) uniform Draw {
    mat4 model;
    vec4 color;
    vec4 color2;
    vec4 params;
};

// A section cap: a square lying on the clip plane (only drawn where the
// stencil says the plane cuts through the body).
void main() {
    vec4 wp = model * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = viewProj * wp;
}
