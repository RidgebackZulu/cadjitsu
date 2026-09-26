#version 440

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vPos;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Cube {
    mat4 mvp;
    mat4 rotation;
    vec4 highlight;
};

layout(binding = 1) uniform sampler2D labels;

void main() {
    vec3 n = normalize(vNormal);
    float light = 0.78 + 0.22 * max(dot(n, normalize(vec3(-0.3, 0.5, 1.0))), 0.0);
    vec4 tex = texture(labels, vUv);
    vec3 c = mix(vec3(0.94, 0.95, 0.97), tex.rgb, tex.a) * light;
    // Hovered face / edge / corner region.
    vec3 region = vec3(abs(vPos.x) > 0.62 ? sign(vPos.x) : 0.0,
                       abs(vPos.y) > 0.62 ? sign(vPos.y) : 0.0,
                       abs(vPos.z) > 0.62 ? sign(vPos.z) : 0.0);
    if(highlight.w > 0.5 && all(equal(region, highlight.xyz)))
        c = mix(c, vec3(0.30, 0.58, 0.92), 0.55);
    fragColor = vec4(c, 1.0);
}
