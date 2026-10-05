#include "render/BuildPlate.h"

#include <cmath>

namespace cad::rt {

namespace {

constexpr float kPi = 3.14159265358979f;

V3 srgb(float r, float g, float b) { return {srgbToLinear(r / 255.0f), srgbToLinear(g / 255.0f), srgbToLinear(b / 255.0f)}; }

// The sheet outline (counter-clockwise, seen from above): a rounded rectangle
// with the grab tab on its front edge.
std::vector<V3> sheetOutline(const PlateLayout &l) {
    std::vector<V3> pts;
    const float x0 = l.cx - l.sheetW / 2, x1 = l.cx + l.sheetW / 2;
    const float y0 = l.sheetCy - l.sheetD / 2, y1 = l.sheetCy + l.sheetD / 2;
    const float r = l.corner;
    auto arc = [&](float cx, float cy, float rad, float a0, float a1, int n) {
        for(int i = 0; i <= n; ++i) {
            const float a = a0 + (a1 - a0) * float(i) / float(n);
            pts.push_back({cx + rad * std::cos(a), cy + rad * std::sin(a), 0});
        }
    };
    // Front edge with the tab (y0), left to right.
    arc(x0 + r, y0 + r, r, kPi, 1.5f * kPi, 8);
    const float tx0 = l.cx - l.tabW / 2, tx1 = l.cx + l.tabW / 2, tr = 3.0f;
    pts.push_back({tx0 - tr, y0, 0});
    arc(tx0 - tr, y0 - tr, tr, 0.5f * kPi, 0.0f, 4);
    arc(tx0 + tr, y0 - l.tabD + tr, tr, kPi, 1.5f * kPi, 4);
    arc(tx1 - tr, y0 - l.tabD + tr, tr, 1.5f * kPi, 2.0f * kPi, 4);
    arc(tx1 + tr, y0 - tr, tr, kPi, 0.5f * kPi, 4);
    pts.push_back({tx1 + tr, y0, 0});
    arc(x1 - r, y0 + r, r, 1.5f * kPi, 2.0f * kPi, 8);
    arc(x1 - r, y1 - r, r, 0.0f, 0.5f * kPi, 8);
    arc(x0 + r, y1 - r, r, 0.5f * kPi, kPi, 8);
    return pts;
}

void addTri(PlateMesh &m, V3 a, V3 b, V3 c, V3 n, int part) {
    const uint32_t i = uint32_t(m.positions.size());
    for(const V3 &v : {a, b, c}) {
        m.positions.push_back(v);
        m.normals.push_back(n);
    }
    m.indices.insert(m.indices.end(), {i, i + 1, i + 2});
    m.part.push_back(part);
}

void addQuad(PlateMesh &m, V3 a, V3 b, V3 c, V3 d, int part) {
    const V3 n = normalize(cross(b - a, c - a));
    addTri(m, a, b, c, n, part);
    addTri(m, a, c, d, n, part);
}

void addBox(PlateMesh &m, float x0, float y0, float z0, float x1, float y1, float z1, int part) {
    addQuad(m, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, part);  // top
    addQuad(m, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, {x0, y0, z0}, part);  // bottom
    addQuad(m, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, part);  // front
    addQuad(m, {x1, y1, z0}, {x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, part);  // back
    addQuad(m, {x0, y1, z0}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, part);  // left
    addQuad(m, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, part);  // right
}

} // namespace

PlateLayout plateLayout(const V3 &bmin, const V3 &bmax, const RenderSettings &settings) {
    PlateLayout l;
    l.kind = settings.plate;
    l.present = settings.plate != BuildPlateKind::None;
    l.top = bmin.z;
    if(settings.placement == Placement::AsModelled) {
        l.cx = l.cy = l.area / 2; // the print area's front-left corner at the origin, as in a slicer
    } else {
        l.cx = 0.5f * (bmin.x + bmax.x);
        l.cy = 0.5f * (bmin.y + bmax.y);
    }
    // The sheet runs 3 mm past the area at the sides, 5 mm at the back and 18 at the front.
    l.sheetCy = l.cy + (5.0f - 18.0f) / 2.0f;
    l.bedCy = l.sheetCy + 2.0f;
    return l;
}

PlateMesh plateMesh(const PlateLayout &l) {
    PlateMesh m;
    if(!l.present) return m;
    const float zt = l.top, zb = l.top - l.sheetT;
    const std::vector<V3> outline = sheetOutline(l);
    const V3 c(l.cx, l.sheetCy, 0);
    const size_t n = outline.size();
    for(size_t i = 0; i < n; ++i) {
        const V3 a = outline[i], b = outline[(i + 1) % n];
        addTri(m, {c.x, c.y, zt}, {a.x, a.y, zt}, {b.x, b.y, zt}, {0, 0, 1}, 0);
        addTri(m, {c.x, c.y, zb}, {b.x, b.y, zb}, {a.x, a.y, zb}, {0, 0, -1}, 1);
        addQuad(m, {a.x, a.y, zb}, {b.x, b.y, zb}, {b.x, b.y, zt}, {a.x, a.y, zt}, 1);
    }
    addBox(m, l.cx - l.bedW / 2, l.bedCy - l.bedD / 2, zb - l.bedT, l.cx + l.bedW / 2, l.bedCy + l.bedD / 2, zb - 0.02f, 2);
    return m;
}

PlateShade plateShade(const PlateLayout &l, int part, const V3 &p, const V3 &ng) {
    PlateShade s;
    s.normal = ng;
    if(part == 1) {
        // Spring steel: the edges and the (PEI) underside, seen only at grazing angles.
        s.albedo = srgb(178, 180, 184);
        s.metalness = 1.0f;
        s.roughness = 0.32f;
        return s;
    }
    if(part == 2) {
        // Black anodised aluminium heated bed, faintly grained.
        const float g = valueNoise(p * 1.7f);
        s.albedo = srgb(30, 31, 34) * (0.9f + 0.2f * g);
        s.roughness = 0.42f;
        return s;
    }
    if(l.kind == BuildPlateKind::SmoothPEI) {
        // Smooth PEI film over the steel: glossy amber-gold with a faint orange peel.
        const float e = 0.5f;
        const V3 q = p * 0.6f;
        const V3 grad(valueNoise(q + V3(e, 0, 0)) - valueNoise(q - V3(e, 0, 0)),
                      valueNoise(q + V3(0, e, 0)) - valueNoise(q - V3(0, e, 0)), 0);
        s.normal = normalize(ng - grad * 0.004f);
        // A clear film over the gold: the plate's shader adds its gloss (a clear coat).
        s.albedo = srgb(176, 128, 58);
        s.metalness = 0.3f;
        s.roughness = 0.06f + 0.03f * valueNoise(p * 0.05f);
        return s;
    }
    // Textured PEI: a powder-coated grain of fused particles (~0.3 mm), golden,
    // with a few flat particles that glint.
    // Each particle is a dome: height falls off with the distance d to its
    // centre, so the slope is along the offset to the centre.
    const float sc = 1.0f / 0.32f;
    float id, fx, fy;
    const float d = cellular(p.x * sc, p.y * sc, id, fx, fy);
    const float t = clampf(d / 0.75f, 0.0f, 1.0f);
    const float dh = (6.0f * t * (1.0f - t)) / 0.75f * (0.75f + 0.5f * id); // -d(height)/d(d)
    const float inv = d > 1e-5f ? 1.0f / d : 0.0f;
    // Height decreases away from the centre: gradient = -dh * (p - c)/d = dh * (fx, fy)/d.
    const float gx = dh * fx * inv, gy = dh * fy * inv;
    const float k = 0.32f * 0.35f;      // grain height relative to its size
    s.normal = normalize(V3(-gx * k, -gy * k, 1.0f));
    const float peak = 1.0f - smoothstepf(0.0f, 0.75f, d);
    s.albedo = srgb(188, 150, 90) * (0.82f + 0.22f * peak + 0.12f * id);
    s.metalness = 0.3f;
    s.roughness = 0.5f;
    if(id > 0.93f) s.roughness = 0.22f; // a grain of the powder coat catching the light
    return s;
}

} // namespace cad::rt
