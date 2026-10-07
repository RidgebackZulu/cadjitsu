#version 440

// A canvas: a reference picture on a plane.
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 vUv;

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
    vec4 color;     // a = opacity
    vec4 color2;
    vec4 params;
};

void main() {
    vUv = uv;
    gl_Position = viewProj * vec4(position, 1.0);
}
