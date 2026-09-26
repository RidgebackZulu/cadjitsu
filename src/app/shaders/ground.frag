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

layout(binding = 2) uniform sampler2D silhouette;

const vec2 disk[12] = vec2[](vec2(0.0, 0.0), vec2(0.54, 0.12), vec2(-0.32, 0.47), vec2(-0.41, -0.35),
                             vec2(0.22, -0.52), vec2(0.93, -0.21), vec2(0.62, 0.74), vec2(-0.12, 0.96),
                             vec2(-0.87, 0.36), vec2(-0.79, -0.58), vec2(0.05, -0.99), vec2(0.71, -0.66));

// Coverage around `uv` within `radius`, from a mip level about as blurry.
float coverage(vec2 uv, float radius, float lod) {
    float s = 0.0;
    for(int i = 0; i < 12; ++i)
        s += textureLod(silhouette, uv + disk[i] * radius, lod).r;
    return s / 12.0;
}

// A soft contact shadow: how much of the model is right above this point,
// blurred at two radii, fading out towards the edge of the ground.
void main() {
    vec4 lp = model * vec4(vWorldPos, 1.0);
    vec2 uv = lp.xy / lp.w * 0.5 + 0.5;
    if(color2.x > 0.5)
        uv.y = 1.0 - uv.y;
    float s = 0.55 * coverage(uv, color2.y, 2.5) + 0.45 * coverage(uv, color2.z, 4.5);
    float fade = 1.0 - smoothstep(0.55, 1.0, length(vLocal));
    fragColor = vec4(color.rgb, color.a * s * fade);
}
