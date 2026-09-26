#version 440

layout(location = 0) in vec3 vWorldPos;
layout(location = 0) out vec4 fragColor;

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

void main() {
    if(misc.x > 0.5 && params.w < 0.5 && dot(clipPlane.xyz, vWorldPos) + clipPlane.w > 0.0)
        discard;
    fragColor = color;
}
