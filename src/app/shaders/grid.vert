#version 440

layout(location = 0) in vec2 corner; // -1..1

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec2 vLocal;

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
    mat4 model;      // grid plane placement (XY plane by default)
    vec4 color;      // minor line colour
    vec4 color2;     // major line colour
    vec4 params;     // x = half extent, y = minor spacing, z = major spacing
};

void main() {
    vec4 w = model * vec4(corner * params.x, 0.0, 1.0);
    vWorldPos = w.xyz;
    vLocal = corner * params.x;
    gl_Position = viewProj * w;
}
