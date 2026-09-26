#version 440

// Thick screen-space lines: one instance per segment, 6 vertices per quad.
layout(location = 0) in vec2 corner;  // x: 0 at p0, 1 at p1; y: -1 / +1 across the line
layout(location = 1) in vec3 p0;
layout(location = 2) in vec3 p1;

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

// Moves a point towards the camera by a fraction (params.y) of its distance,
// so edges win the depth test against the faces they bound.
vec4 towardEye(vec4 w) {
    vec3 toEye = eyePos.w > 0.5 ? -eyeDir.xyz * length(eyePos.xyz - w.xyz) : eyePos.xyz - w.xyz;
    return vec4(w.xyz + toEye * params.y, 1.0);
}

void main() {
    vec4 w0 = model * vec4(p0, 1.0);
    vec4 w1 = model * vec4(p1, 1.0);
    vec4 b0 = towardEye(w0);
    vec4 b1 = towardEye(w1);
    vec4 c0 = viewProj * b0;
    vec4 c1 = viewProj * b1;
    // Keep both ends in front of the camera.
    c0.w = max(c0.w, 1e-5);
    c1.w = max(c1.w, 1e-5);
    vec2 halfVp = viewport.xy * 0.5;
    vec2 s0 = c0.xy / c0.w * halfVp;
    vec2 s1 = c1.xy / c1.w * halfVp;
    vec2 d = s1 - s0;
    float len = length(d);
    vec2 dir = len > 1e-6 ? d / len : vec2(1.0, 0.0);
    vec2 nrm = vec2(-dir.y, dir.x);
    float halfWidth = params.x * 0.5;
    vec4 c = corner.x < 0.5 ? c0 : c1;
    vec2 offset = nrm * corner.y * halfWidth + dir * (corner.x < 0.5 ? -halfWidth : halfWidth) * 0.5;
    c.xy += offset / halfVp * c.w;
    vWorldPos = corner.x < 0.5 ? w0.xyz : w1.xyz;
    gl_Position = c;
}
