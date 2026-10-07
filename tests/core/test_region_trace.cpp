// Tracing parts in pictures: regions by colour, their outlines, and outlines
// fitted with lines and arcs (and turned into sketch curves).
#include <doctest.h>

#include "TestModels.h"
#include "image/RegionTrace.h"
#include "sketch/ContourFit.h"
#include "sketch/SketchSolver.h"
#include "sketch/SketchText.h"

#include <cmath>
#include <functional>

using namespace cadtest;

namespace {

struct Picture {
    int w, h;
    std::vector<uint8_t> px;
    Picture(int w_, int h_, std::function<bool(double, double)> inside, int fg = 40, int bg = 235) : w(w_), h(h_) {
        px.resize(size_t(w) * size_t(h) * 4);
        for(int y = 0; y < h; ++y)
            for(int x = 0; x < w; ++x) {
                const int v = inside(x + 0.5, y + 0.5) ? fg : bg;
                uint8_t *p = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
                p[0] = p[1] = p[2] = uint8_t(v);
                p[3] = 255;
            }
    }
    ImageView view() const { return {px.data(), w, h, w * 4}; }
};

// A 200 x 120 plate with 20 px round corners, centred at (150, 100), with a
// 15 px hole at (100, 100).
bool roundedPlate(double x, double y) {
    const double hx = 100, hy = 60, r = 20;
    const double dx = std::fabs(x - 150), dy = std::fabs(y - 100);
    if(dx > hx || dy > hy) return false;
    if(dx > hx - r && dy > hy - r && std::hypot(dx - (hx - r), dy - (hy - r)) > r) return false;
    return std::hypot(x - 100, y - 100) > 15;
}

} // namespace

TEST_CASE("trace: a region by colour, its outline and its hole") {
    const Picture pic(300, 200, roundedPlate);
    const Mask m = regionAt(pic.view(), 200, 100, 0.2);
    CHECK(m.count() > 20000);
    CHECK_FALSE(m.at(100, 100)); // the hole is not part of it
    CHECK_FALSE(m.at(5, 5));
    const PixelBox b = boundingBox(m);
    CHECK(b.x0 == 50);
    CHECK(b.x1 == 249);
    CHECK(b.height() == 120);
    const auto loops = traceLoops(m, 20);
    REQUIRE(loops.size() == 2);
    CHECK_FALSE(loops[0].hole);
    CHECK(loops[1].hole);
    // Areas close to the shapes' (the outline runs along pixel edge middles).
    CHECK(loops[0].area == doctest::Approx(200 * 120 - 4 * 400 * (1 - kPi / 4)).epsilon(0.02));
    CHECK(loops[1].area == doctest::Approx(kPi * 225).epsilon(0.08));
    // The foreground from the border colour is the same plate.
    const Mask f = foreground(pic.view(), 0.2);
    CHECK(std::abs(int(f.count()) - int(m.count())) < 50);
    CHECK(regionAt(pic.view(), -1, 0, 0.2).count() == 0);
}

TEST_CASE("fit: a rounded plate becomes 4 lines and 4 arcs; a hole a circle") {
    const Picture pic(300, 200, roundedPlate);
    const auto loops = traceLoops(regionAt(pic.view(), 200, 100, 0.2), 20);
    REQUIRE(loops.size() == 2);
    const FitLoop outer = fitContour(loops[0].points, 1.2);
    REQUIRE_FALSE(outer.circle);
    int lines = 0, arcs = 0;
    for(const auto &s : outer.segments) (s.arc ? arcs : lines)++;
    CHECK(lines == 4);
    CHECK(arcs == 4);
    for(const auto &s : outer.segments)
        if(s.arc) CHECK(s.radius == doctest::Approx(20).epsilon(0.1));
    const FitLoop hole = fitContour(loops[1].points, 1.2);
    REQUIRE(hole.circle);
    CHECK(hole.radius == doctest::Approx(15).epsilon(0.05));
    CHECK(distance(hole.centre, {100, 100}) < 0.6);

    // Into a sketch at 0.5 mm a pixel, y up: a solvable plate of the right size.
    Sketch s;
    std::vector<SuggestedConstraint> hints;
    auto map = [](Vec2 p) { return Vec2(p.x * 0.5, -p.y * 0.5); };
    const auto curves = addFittedLoop(s, outer, map, hints);
    addFittedLoop(s, hole, map, hints);
    CHECK(curves.size() == 8);
    int hv = 0, tangent = 0;
    for(const auto &h : hints) {
        hv += h.type == SkCon::Horizontal || h.type == SkCon::Vertical;
        tangent += h.type == SkCon::Tangent;
    }
    CHECK(hv == 4);
    CHECK(tangent == 8);
    for(const auto &h : hints) s.addConstraint(h.type, h.e1, h.e2);
    REQUIRE(solveSketch(s, [](const std::string &, double &) { return false; }).ok);
    const auto profiles = sketchProfiles(s);
    REQUIRE(profiles.size() == 2); // the plate with its hole, and the hole
    double plate = 0;
    for(const auto &p : profiles) plate = std::max(plate, std::abs(p.outer.area));
    CHECK(plate == doctest::Approx(100 * 60 - 4 * 100 * (1 - kPi / 4)).epsilon(0.03));
}

TEST_CASE("fit: a triangle stays three lines; a circle fit is exact on points") {
    std::vector<Vec2> tri;
    for(int k = 0; k <= 50; ++k) tri.push_back(Vec2(0, 0) + Vec2(100, 0) * (k / 50.0));
    for(int k = 1; k <= 50; ++k) tri.push_back(Vec2(100, 0) + Vec2(-50, 80) * (k / 50.0));
    for(int k = 1; k < 50; ++k) tri.push_back(Vec2(50, 80) + Vec2(-50, -80) * (k / 50.0));
    const FitLoop f = fitContour(tri, 0.5);
    REQUIRE_FALSE(f.circle);
    CHECK(f.segments.size() == 3);
    std::vector<Vec2> ring;
    for(int k = 0; k < 40; ++k) ring.push_back(Vec2(7, -3) + Vec2(std::cos(k * 0.157), std::sin(k * 0.157)) * 12);
    Vec2 c;
    double r;
    REQUIRE(fitCircle(ring, c, r));
    CHECK(distance(c, {7, -3}) < 1e-9);
    CHECK(r == doctest::Approx(12));
}

TEST_CASE("fit: a corner blurred into a tiny bevel stays a sharp corner") {
    // An L 400 across whose inner corner is cut by a 1.5 unit bevel (a
    // blurred photo), fitted to 1 unit.
    const std::vector<Vec2> corners = {{0, 0},      {400, 0},     {400, 100}, {101.5, 100},
                                       {100, 101.5}, {100, 400}, {0, 400}};
    std::vector<Vec2> loop;
    for(size_t i = 0; i < corners.size(); ++i) {
        const Vec2 a = corners[i], b = corners[(i + 1) % corners.size()];
        const int k = std::max(1, int(distance(a, b)));
        for(int q = 0; q < k; ++q) loop.push_back(a + (b - a) * (double(q) / k));
    }
    const FitLoop f = fitContour(loop, 1.0);
    REQUIRE(f.segments.size() == 6);
    bool sharp = false;
    for(const auto &s : f.segments) {
        CHECK_FALSE(s.arc);
        sharp |= distance(s.a, {100, 100}) < 0.2;
    }
    CHECK(sharp);
}
