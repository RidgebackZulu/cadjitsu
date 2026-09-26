#version 440

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;
layout(location = 2) in vec3 vPos;
layout(location = 3) in vec3 vObjNormal;
layout(location = 4) flat in vec3 vRegion;
layout(location = 5) flat in float vMaterial;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Cube {
    mat4 mvp;
    mat4 rotation;
    vec4 highlight;
};

layout(binding = 1) uniform sampler2D labels;

// Soft studio lighting in view space: a key light from the upper left, a
// fill from the right and a little rim.
float lighting(vec3 n) {
    vec3 key = normalize(vec3(-0.45, 0.65, 0.85));
    vec3 fill = normalize(vec3(0.7, -0.2, 0.6));
    float l = 0.80 + 0.20 * max(dot(n, key), 0.0) + 0.07 * max(dot(n, fill), 0.0);
    float rim = pow(1.0 - max(n.z, 0.0), 3.0);
    return min(l + 0.05 * rim, 1.04);
}

void main() {
    vec3 n = normalize(vNormal);
    int m = int(vMaterial + 0.5);
    vec3 c;
    if(m == 0) {
        // The cube: satin off-white plastic.
        vec3 base = vec3(0.975, 0.98, 0.99);
        bool face = abs(vObjNormal.x) + abs(vObjNormal.y) + abs(vObjNormal.z) < 1.01;
        if(!face) base = vec3(0.90, 0.915, 0.94); // bevels a shade deeper
        vec4 tex = texture(labels, vUv);
        c = mix(base, tex.rgb / max(tex.a, 0.001), tex.a);
        if(face) {
            // Gentle occlusion towards the face's rim.
            vec3 a = abs(vPos) * (vec3(1.0) - abs(vObjNormal));
            float e = max(max(a.x, a.y), a.z) / 0.78;
            c *= 1.0 - 0.07 * smoothstep(0.55, 1.0, e);
        }
        c *= lighting(n);
        if(highlight.w > 0.5 && all(equal(vRegion, highlight.xyz))) {
            c = mix(c, vec3(0.33, 0.60, 0.95), 0.55) + vec3(0.05, 0.07, 0.10);
        }
    } else if(m == 1) {
        c = vec3(0.80, 0.84, 0.90) * (0.9 + 0.1 * lighting(n));
    } else if(m == 2) {
        c = vec3(0.42, 0.50, 0.62);
    } else if(m == 3) {
        float a = texture(labels, vUv).a;
        c = mix(vec3(0.80, 0.84, 0.90) * (0.9 + 0.1 * lighting(n)), vec3(0.20, 0.27, 0.38), a);
    } else if(m == 4) {
        c = vec3(0.90, 0.26, 0.21) * lighting(n);
    } else if(m == 5) {
        c = vec3(0.22, 0.68, 0.30) * lighting(n);
    } else {
        c = vec3(0.19, 0.46, 0.90) * lighting(n);
    }
    fragColor = vec4(c, 1.0);
}
