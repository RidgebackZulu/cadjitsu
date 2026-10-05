#version 440

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Buf {
    vec4 topColor;
    vec4 bottomColor;
    float ndcYUp;
};

layout(binding = 1) uniform sampler2D traced;

// Premultiplied: the backdrop shows where the rays met nothing.
void main() {
    fragColor = texture(traced, vUv);
}
