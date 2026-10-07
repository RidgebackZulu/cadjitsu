#version 440

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 1) uniform Draw {
    mat4 model;
    vec4 color;     // a = opacity
    vec4 color2;
    vec4 params;
};

layout(binding = 2) uniform sampler2D picture;

void main() {
    vec4 c = texture(picture, vUv);
    fragColor = vec4(c.rgb, c.a * color.a);
}
