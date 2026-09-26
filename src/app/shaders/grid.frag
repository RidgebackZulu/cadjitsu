#version 440

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec2 vLocal;
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

float gridLine(vec2 coord) {
    vec2 g = abs(fract(coord - 0.5) - 0.5) / max(fwidth(coord), vec2(1e-6));
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main() {
    vec2 p = vLocal; // grid coordinates in the plane's local frame
    float minor = gridLine(p / params.y);
    float major = gridLine(p / params.z);
    vec4 c = vec4(0.0);
    c = mix(c, color, minor * color.a);
    c = mix(c, color2, major * color2.a);
    // Axis lines: X red, Y green.
    vec2 w = fwidth(p) * 1.2;
    float ax = 1.0 - smoothstep(0.0, w.y, abs(p.y));
    float ay = 1.0 - smoothstep(0.0, w.x, abs(p.x));
    c = mix(c, vec4(0.82, 0.25, 0.22, 0.9), ax);
    c = mix(c, vec4(0.30, 0.66, 0.30, 0.9), ay);
    // Fade towards the edge of the grid and at grazing angles.
    float edge = 1.0 - smoothstep(0.55, 1.0, length(p) / params.x);
    vec3 n = normalize(mat3(model) * vec3(0.0, 0.0, 1.0));
    vec3 v = eyePos.w > 0.5 ? -eyeDir.xyz : normalize(eyePos.xyz - vWorldPos);
    float grazing = smoothstep(0.02, 0.2, abs(dot(n, v)));
    c.a *= edge * grazing;
    if(c.a < 0.003)
        discard;
    fragColor = vec4(c.rgb * c.a, c.a); // premultiplied
}
