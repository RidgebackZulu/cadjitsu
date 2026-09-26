#version 440

// Screen-aligned point sprites: one instance per point, 6 vertices per quad.
layout(location = 0) in vec2 corner;  // -1..1
layout(location = 1) in vec3 center;

layout(location = 0) out vec2 vCorner;
layout(location = 1) out vec3 vWorldPos;

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
    vec4 w = model * vec4(center, 1.0);
    vec3 toEye = eyePos.w > 0.5 ? -eyeDir.xyz * length(eyePos.xyz - w.xyz) : eyePos.xyz - w.xyz;
    vec4 c = viewProj * vec4(w.xyz + toEye * params.y, 1.0);
    c.xy += corner * params.x * 0.5 / (viewport.xy * 0.5) * c.w;
    vCorner = corner;
    vWorldPos = w.xyz;
    gl_Position = c;
}
