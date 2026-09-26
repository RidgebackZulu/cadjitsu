#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;

layout(std140, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 eyePos;     // xyz = eye, w = 1 for orthographic
    vec4 eyeDir;     // xyz = view direction
    vec4 lightDir;   // xyz = key light direction (towards the light)
    vec4 viewport;   // framebuffer width, height, 1/width, 1/height
    vec4 clipPlane;  // n.xyz, d: fragments with dot(n, p) + d > 0 are cut away
    vec4 misc;       // x = clip enabled
};

layout(std140, binding = 1) uniform Draw {
    mat4 model;
    vec4 color;
    vec4 color2;
    vec4 params;     // x = line width / point size (px), y = depth bias, z = lit (1) or flat (0)
};

void main() {
    vec4 wp = model * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    vNormal = mat3(model) * normal;
    // Overlays (highlights) are nudged towards the camera to win against the face.
    vec3 toEye = eyePos.w > 0.5 ? -eyeDir.xyz * length(eyePos.xyz - wp.xyz) : eyePos.xyz - wp.xyz;
    gl_Position = viewProj * vec4(wp.xyz + toEye * params.y, 1.0);
}
