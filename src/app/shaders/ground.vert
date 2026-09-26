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
    mat4 model;   // top-down view of the model: world -> silhouette texture clip space
    vec4 color;   // shadow colour and strength (alpha)
    vec4 color2;  // x = sample y flipped, y = small blur radius, z = large blur radius (uv)
    vec4 params;  // xy = centre, z = half size, w = height of the ground
};

// The ground under the model in the rendered style.
void main() {
    vec3 w = vec3(params.xy + corner * params.z, params.w);
    vWorldPos = w;
    vLocal = corner;
    gl_Position = viewProj * vec4(w, 1.0);
}
