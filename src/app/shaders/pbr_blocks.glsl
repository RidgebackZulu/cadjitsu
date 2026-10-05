// The uniform blocks of the Rendered style's shaders (vertex and fragment).

layout(std140, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 eyePos;
    vec4 eyeDir;
    vec4 lightDir;
    vec4 viewport;
    vec4 clipPlane;
    vec4 misc;       // x = clip on, y = overlay depth offset, z = linear output (plate reflection)
};

layout(std140, binding = 1) uniform Draw {
    mat4 model;
    vec4 color;
    vec4 color2;
    vec4 params;     // x = thickness pass: reference distance (mm)
    vec4 m0; // albedo.rgb (linear), ior
    vec4 m1; // specTint.rgb, metalness
    vec4 m2; // roughness, roughness across the layers, sheen, transmission
    vec4 m3; // absorption per mm .rgb, layer line strength
    vec4 m4; // micro grain, pass (0 opaque, 1 transmittance, 2 light of a translucent body), shadow thickness (mm), scattering mean free path (mm)
};

layout(std140, binding = 4) uniform Scene {
    mat4 lightViewProj;  // world -> shadow map clip
    mat4 reflViewProj;   // world -> plate reflection clip
    mat4 contactViewProj;// world -> top-down silhouette clip (contact shadow)
    vec4 keyDir;         // towards the key light, w = tan(angular radius)
    vec4 keyIrr;         // key irradiance rgb, w = exposure
    vec4 surface;        // layer height, line width, layer lines on (strength scale), plate z
    vec4 plateInfo;      // kind (0 textured, 1 smooth, 2 none), reflection on, contact on, env max lod
    vec4 shadowInfo;     // texel (uv), world depth per stored unit, world size of the map, on
    vec4 flips;          // shadow and contact map y flip
    vec4 sh[9];          // irradiance SH (rgb)
};
