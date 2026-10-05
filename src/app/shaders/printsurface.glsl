// GLSL twin of src/core/render/RenderMath.h (noise), PrintSurface.cpp and
// BuildPlate.cpp (plate surfaces). Keep the two in step.

float hash3(int x, int y, int z) {
    uint h = uint(x) * 0x8da6b343u ^ uint(y) * 0xd8163841u ^ uint(z) * 0xcb1ab31fu;
    h = h * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return float(h & 0x00ffffffu) / 16777216.0;
}

float valueNoise(vec3 p) {
    vec3 f = floor(p);
    ivec3 i = ivec3(f);
    vec3 u = p - f;
    u = u * u * (3.0 - 2.0 * u);
    float x00 = mix(hash3(i.x, i.y, i.z), hash3(i.x + 1, i.y, i.z), u.x);
    float x10 = mix(hash3(i.x, i.y + 1, i.z), hash3(i.x + 1, i.y + 1, i.z), u.x);
    float x01 = mix(hash3(i.x, i.y, i.z + 1), hash3(i.x + 1, i.y, i.z + 1), u.x);
    float x11 = mix(hash3(i.x, i.y + 1, i.z + 1), hash3(i.x + 1, i.y + 1, i.z + 1), u.x);
    return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
}

// Distance to the nearest feature point; its cell id; the offset to it.
float cellular(vec2 p, out float cellId, out vec2 toFeature) {
    vec2 f = floor(p);
    float best = 8.0;
    cellId = 0.0;
    toFeature = vec2(0.0);
    for(int j = -1; j <= 1; ++j)
        for(int i = -1; i <= 1; ++i) {
            int cx = int(f.x) + i, cy = int(f.y) + j;
            vec2 o = vec2(hash3(cx, cy, 11), hash3(cx, cy, 23));
            vec2 d = vec2(float(cx), float(cy)) + o - p;
            float dd = dot(d, d);
            if(dd < best) {
                best = dd;
                cellId = hash3(cx, cy, 37);
                toFeature = d;
            }
        }
    return sqrt(best);
}

float beadProfile(float g, out float slope) {
    float u = 2.0 * clamp(g, 0.0, 1.0) - 1.0;
    float r = sqrt(max(1.0 - u * u, 1e-4));
    slope = clamp(-2.0 * u / r, -3.0, 3.0);
    return r;
}

struct SurfaceSample {
    vec3 normal;
    vec3 tangent;
    float cavity;
    float roughnessAdd;
    float acrossAdd;
};

// layerHeight, lineWidth, strength (0 = smooth), micro grain, plate z, plate kind, footprint (mm).
SurfaceSample printSurface(vec3 p, vec3 ng, float layerHeight, float lineWidth, float strength, float micro,
                           float plateZ, int plateKind, float footprint) {
    SurfaceSample o;
    vec3 Z = vec3(0.0, 0.0, 1.0);
    float cz = ng.z;
    float h = max(layerHeight, 1e-3), w = max(lineWidth, 1e-3);
    float fade = 1.0 - smoothstep(0.35, 1.2, footprint / h);
    o.roughnessAdd = (1.0 - fade) * 0.12 * strength;
    o.acrossAdd = 0.0;
    float tz = (p.z - plateZ) / h;
    float layer = floor(tz);
    int k = int(layer);
    float f = tz - layer;
    float jitter = 0.8 + 0.4 * hash3(k, 7, 3);
    float A = 0.18 * strength * jitter * fade;
    vec3 n = ng;
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    float cavity = 1.0;
    if(cz < -0.98 && abs(p.z - plateZ) < 0.6 * h) {
        if(plateKind == 0) {
            float e = 0.02;
            vec3 q = p * 5.0;
            float gx = valueNoise(q + vec3(e, 0, 0)) - valueNoise(q - vec3(e, 0, 0));
            float gy = valueNoise(q + vec3(0, e, 0)) - valueNoise(q - vec3(0, e, 0));
            n = normalize(ng + vec3(gx, gy, 0.0) * (0.35 / (2.0 * e)) * 0.05 * fade);
            o.roughnessAdd += 0.25;
        }
    } else if(abs(cz) > 0.98) {
        bool up = cz > 0.0;
        int topLayer = up ? int(floor(tz - 0.5)) : k;
        vec3 dir = (topLayer & 1) != 0 ? vec3(0.70710678, -0.70710678, 0.0) : vec3(0.70710678, 0.70710678, 0.0);
        vec3 across = vec3(-dir.y, dir.x, 0.0);
        float v = dot(p, across) / w;
        float slope;
        float g = v - floor(v);
        float bump = beadProfile(g, slope);
        float amp = (up ? 0.05 : 0.14) * strength * fade * h / w;
        n = normalize(ng - across * (slope * amp * (up ? 1.0 : -1.0)));
        cavity = 1.0 - (up ? 0.06 : 0.12) * strength * fade * (1.0 - bump);
        tangent = dir;
        o.acrossAdd = (1.0 - fade) * 0.15 * strength;
        if(!up)
            o.roughnessAdd += 0.12;
    } else {
        vec3 nh = normalize(vec3(ng.xy, 0.0));
        tangent = normalize(cross(Z, nh));
        // Bead crowns too fine to see still spread reflections up and down the wall.
        o.acrossAdd = (1.0 - fade) * 0.4 * strength;
        float q = min(cz * cz, 0.92);
        float riser = 1.0;
        vec3 tread = cz > 0.0 ? Z : -Z;
        if(q > 0.01) {
            float edge = 0.06;
            riser = smoothstep(q - edge, q + edge, f);
            f = clamp((f - q) / (1.0 - q), 0.0, 1.0);
        }
        float slope;
        float bump = beadProfile(f, slope);
        vec3 riserN = normalize(nh - Z * (slope * A / max(1.0 - q, 0.08)));
        n = normalize(mix(tread, riserN, riser));
        n = normalize(mix(ng, n, fade));
        cavity = 1.0 - 0.22 * strength * fade * riser * (1.0 - bump);
    }
    if(micro > 0.0) {
        float e = 0.01;
        vec3 q = p * 22.0;
        vec3 grad = vec3(valueNoise(q + vec3(e, 0, 0)) - valueNoise(q - vec3(e, 0, 0)),
                         valueNoise(q + vec3(0, e, 0)) - valueNoise(q - vec3(0, e, 0)),
                         valueNoise(q + vec3(0, 0, e)) - valueNoise(q - vec3(0, 0, e)));
        vec3 g = grad * (1.0 / (2.0 * e)) * (0.06 * micro) * fade;
        g = g - n * dot(g, n);
        n = normalize(n + g);
    }
    o.normal = n;
    o.tangent = normalize(tangent - n * dot(tangent, n));
    o.cavity = clamp(cavity, 0.0, 1.0);
    return o;
}

vec3 srgbToLinear3(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

struct PlateShade {
    vec3 albedo;
    vec3 normal;
    float roughness;
    float metalness;
};

// part: 0 PEI top, 1 steel, 2 bed. kind: 0 textured, 1 smooth.
PlateShade plateShade(int kind, int part, vec3 p, vec3 ng, float footprint) {
    PlateShade s;
    s.normal = ng;
    s.metalness = 0.0;
    if(part == 1) {
        s.albedo = srgbToLinear3(vec3(178.0, 180.0, 184.0) / 255.0);
        s.metalness = 1.0;
        s.roughness = 0.32;
        return s;
    }
    if(part == 2) {
        float g = valueNoise(p * 1.7);
        s.albedo = srgbToLinear3(vec3(30.0, 31.0, 34.0) / 255.0) * (0.9 + 0.2 * g);
        s.roughness = 0.42;
        return s;
    }
    if(kind == 1) {
        float e = 0.5;
        vec3 q = p * 0.6;
        vec3 grad = vec3(valueNoise(q + vec3(e, 0, 0)) - valueNoise(q - vec3(e, 0, 0)),
                         valueNoise(q + vec3(0, e, 0)) - valueNoise(q - vec3(0, e, 0)), 0.0);
        s.normal = normalize(ng - grad * 0.004);
        s.albedo = srgbToLinear3(vec3(176.0, 128.0, 58.0) / 255.0);
        s.metalness = 0.3;
        s.roughness = 0.06 + 0.03 * valueNoise(p * 0.05);
        return s;
    }
    float sc = 1.0 / 0.32;
    float id;
    vec2 toF;
    float d = cellular(p.xy * sc, id, toF);
    float t = clamp(d / 0.75, 0.0, 1.0);
    float dh = (6.0 * t * (1.0 - t)) / 0.75 * (0.75 + 0.5 * id);
    float inv = d > 1e-5 ? 1.0 / d : 0.0;
    vec2 grad = dh * toF * inv;
    float k = 0.32 * 0.35;
    // Grain smaller than a pixel: flat, but rougher.
    float fade = 1.0 - smoothstep(0.25, 0.9, footprint / 0.32);
    s.normal = normalize(vec3(-grad * k * fade, 1.0));
    float peak = 1.0 - smoothstep(0.0, 0.75, d);
    s.albedo = srgbToLinear3(vec3(188.0, 150.0, 90.0) / 255.0) * (0.82 + (0.22 * peak + 0.12 * id) * mix(0.6, 1.0, fade));
    s.metalness = 0.3;
    s.roughness = mix(0.62, 0.5, fade);
    if(id > 0.93 && fade > 0.2) {
        // A grain of the powder coat catching the light.
        s.roughness = mix(s.roughness, 0.22, fade);
    }
    return s;
}
