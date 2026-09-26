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
    vec4 color;   // cap
    vec4 color2;  // hatch lines
    vec4 params;  // x = hatch spacing (px), y = hatch line width (px)
};

// Diagonal hatching at a constant screen density, as Fusion 360 draws
// section caps.
void main() {
    float t = mod(gl_FragCoord.x + gl_FragCoord.y, params.x);
    fragColor = t < params.y ? color2 : color;
}
