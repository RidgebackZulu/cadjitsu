#version 440

// Full-screen triangle; the gradient runs from the top edge (t = 0) to the bottom (t = 1).
layout(location = 0) out float vT;

layout(std140, binding = 0) uniform Buf {
    vec4 topColor;
    vec4 bottomColor;
    float ndcYUp; // 1 when NDC +Y points up (GL, Metal, D3D), -1 otherwise (Vulkan)
};

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = uv * 2.0 - 1.0;
    vT = 0.5 - 0.5 * ndc.y * ndcYUp;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
