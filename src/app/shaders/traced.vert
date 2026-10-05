#version 440

// Full-screen triangle for the path-traced image (row 0 at the top).
layout(location = 0) out vec2 vUv;

layout(std140, binding = 0) uniform Buf {
    vec4 topColor;
    vec4 bottomColor;
    float ndcYUp;
};

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = uv * 2.0 - 1.0;
    vUv = vec2(uv.x, 0.5 - 0.5 * ndc.y * ndcYUp);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
