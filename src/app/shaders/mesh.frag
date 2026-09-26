#version 440

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
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

void main() {
    if(misc.x > 0.5 && dot(clipPlane.xyz, vWorldPos) + clipPlane.w > 0.0)
        discard;
    if(params.z < 0.5) {
        fragColor = color;
        return;
    }
    vec3 V = eyePos.w > 0.5 ? -eyeDir.xyz : normalize(eyePos.xyz - vWorldPos);
    vec3 N = normalize(vNormal);
    bool backFace = dot(N, V) < 0.0;
    if(backFace)
        N = -N;
    vec3 L = normalize(lightDir.xyz);
    vec3 H = normalize(L + V);
    float diffuse = max(dot(N, L), 0.0);
    float fill = max(dot(N, V), 0.0);
    float hemi = 0.5 + 0.5 * N.z;
    vec3 ambient = mix(vec3(0.42, 0.40, 0.38), vec3(0.62, 0.66, 0.72), hemi);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.22;
    vec3 base = backFace ? color2.rgb : color.rgb;
    vec3 lit = base * (ambient * 0.55 + diffuse * 0.45 + fill * 0.25) + vec3(spec);
    fragColor = vec4(lit, color.a);
}
