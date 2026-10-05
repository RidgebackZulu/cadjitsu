// The printed surface model (layer lines, staircase, top skins) and the build plate.
#include <doctest.h>

#include "render/BuildPlate.h"
#include "render/PrintSurface.h"

#include <cmath>

using namespace cad;
using namespace cad::rt;

TEST_CASE("walls are ridged once per layer, with seams between layers") {
    SurfaceParams s;
    s.layerHeight = 0.2f;
    const V3 n(1, 0, 0);
    // Below a bead's crown the normal tips down, above it up; the same each layer.
    for(int layer = 3; layer < 8; ++layer) {
        const float z0 = layer * 0.2f;
        const SurfaceSample lo = printSurface({5, 1, z0 + 0.05f}, n, s);
        const SurfaceSample mid = printSurface({5, 1, z0 + 0.10f}, n, s);
        const SurfaceSample hi = printSurface({5, 1, z0 + 0.15f}, n, s);
        CHECK(lo.normal.z < -0.05f);
        CHECK(hi.normal.z > 0.05f);
        CHECK(std::abs(mid.normal.z) < 0.02f);
        // Darker in the seam than on the crown.
        CHECK(printSurface({5, 1, z0 + 0.005f}, n, s).cavity < mid.cavity);
        CHECK(lo.tangent.z == doctest::Approx(0.0f).epsilon(1e-5)); // the extrusion runs level
    }
    // Strength 0: a smooth wall.
    s.strength = 0.0f;
    const SurfaceSample flat = printSurface({5, 1, 0.65f}, n, s);
    CHECK(flat.normal.x == doctest::Approx(1.0f));
    // Detail far below the pixel size fades into roughness.
    s.strength = 1.0f;
    s.footprint = 2.0f;
    const SurfaceSample far = printSurface({5, 1, 0.65f}, n, s);
    CHECK(far.normal.x == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(far.roughnessAdd > 0.05f);
}

TEST_CASE("slopes step: flat treads take cos^2 of the surface") {
    SurfaceParams s;
    s.layerHeight = 0.2f;
    for(float deg : {30.0f, 60.0f}) {
        const float a = deg * 3.14159265f / 180.0f;     // surface tilt from vertical toward up
        const V3 n = normalize(V3(std::cos(a), 0, std::sin(a)));
        int tread = 0, total = 0;
        for(int i = 0; i < 2000; ++i) {
            const float z = 1.0f + i * 0.0013f;
            const V3 nn = printSurface({0, 0, z}, n, s).normal;
            tread += nn.z > 0.97f;
            ++total;
        }
        const float share = float(tread) / float(total);
        CHECK(share == doctest::Approx(n.z * n.z).epsilon(0.25));
    }
}

TEST_CASE("top skins run at 45 degrees, turning each layer") {
    SurfaceParams s;
    s.layerHeight = 0.2f;
    const V3 up(0, 0, 1);
    const V3 t1 = printSurface({3, 4, 1.0f}, up, s).tangent;  // top of layer 4
    const V3 t2 = printSurface({3, 4, 1.2f}, up, s).tangent;  // top of layer 5
    CHECK(std::abs(t1.x) == doctest::Approx(0.7071f).epsilon(1e-3));
    CHECK(std::abs(dot(t1, t2)) < 1e-4f);
    // Ridges one line width apart: the same normal a line width further across.
    const V3 across(-t1.y, t1.x, 0);
    const V3 a = printSurface(V3(3, 4, 1.0f) + across * 0.1f, up, s).normal;
    const V3 b = printSurface(V3(3, 4, 1.0f) + across * (0.1f + s.lineWidth), up, s).normal;
    CHECK(dot(a, b) == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("the U1 plate lies under the model") {
    RenderSettings rs;
    const V3 lo(10, 20, 5), hi(50, 40, 25);
    PlateLayout l = plateLayout(lo, hi, rs);
    CHECK(l.present);
    CHECK(l.top == 5.0f);
    CHECK(l.cx == 30.0f);
    CHECK(l.cy == 30.0f);
    CHECK(l.area == 270.0f);
    const PlateMesh m = plateMesh(l);
    REQUIRE(!m.indices.empty());
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, topArea = 0;
    for(size_t t = 0; t < m.part.size(); ++t) {
        const V3 a = m.positions[m.indices[3 * t]], b = m.positions[m.indices[3 * t + 1]], c = m.positions[m.indices[3 * t + 2]];
        if(m.part[t] != 0) continue;
        CHECK(a.z == 5.0f);
        CHECK(m.normals[m.indices[3 * t]].z == 1.0f);
        topArea += 0.5f * length(cross(b - a, c - a));
        for(const V3 &v : {a, b, c}) {
            minx = std::min(minx, v.x), maxx = std::max(maxx, v.x);
            miny = std::min(miny, v.y), maxy = std::max(maxy, v.y);
        }
    }
    CHECK(maxx - minx == doctest::Approx(276.0f).epsilon(1e-3));
    CHECK(maxy - miny == doctest::Approx(293.0f + 9.0f).epsilon(1e-3)); // with the grab tab
    CHECK(topArea > 270.0f * 270.0f);
    CHECK(topArea < 276.0f * 302.0f);
    // The print area lies within the sheet.
    CHECK(l.cx - 135 > minx);
    CHECK(l.cy + 135 < maxy);
    CHECK(l.cy - 135 > miny);
    // As modelled: the print area's front-left corner at the origin.
    rs.placement = Placement::AsModelled;
    l = plateLayout(lo, hi, rs);
    CHECK(l.cx == 135.0f);
    // No plate.
    rs.plate = BuildPlateKind::None;
    CHECK_FALSE(plateLayout(lo, hi, rs).present);
    CHECK(plateMesh(plateLayout(lo, hi, rs)).indices.empty());
}

TEST_CASE("plate surfaces: textured grain, glossy smooth PEI, steel and bed") {
    RenderSettings rs;
    PlateLayout l = plateLayout({0, 0, 0}, {10, 10, 10}, rs);
    float minN = 1, rough = 0;
    for(int i = 0; i < 200; ++i) {
        const PlateShade s = plateShade(l, 0, {i * 0.037f, i * 0.011f, 0}, {0, 0, 1});
        minN = std::min(minN, s.normal.z);
        rough += s.roughness / 200;
        CHECK(s.albedo.x > s.albedo.z); // golden
    }
    CHECK(minN < 0.995f);   // the grain tilts the surface
    CHECK(rough > 0.35f);
    rs.plate = BuildPlateKind::SmoothPEI;
    l = plateLayout({0, 0, 0}, {10, 10, 10}, rs);
    const PlateShade smooth = plateShade(l, 0, {1, 2, 0}, {0, 0, 1});
    CHECK(smooth.roughness < 0.12f);
    CHECK(smooth.normal.z > 0.9999f);
    CHECK(plateShade(l, 1, {0, 0, 0}, {1, 0, 0}).metalness == 1.0f);
    CHECK(plateShade(l, 2, {0, 0, -3}, {0, 0, 1}).albedo.x < 0.05f);
}
