#version 440

layout(location = 0) in float vT;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Buf {
    vec4 topColor;
    vec4 bottomColor;
    float ndcYUp;
};

void main() {
    fragColor = mix(topColor, bottomColor, clamp(vT, 0.0, 1.0));
}
