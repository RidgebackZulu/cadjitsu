// Profile detection: closed regions formed by sketch curves.
#include <doctest.h>

#include "sketch/ProfileBuilder.h"
#include "sketch/Sketch.h"

#include <algorithm>
#include <cmath>

using namespace cad;

namespace {

std::vector<double> sortedAreas(const ProfileBuildResult &r) {
    std::vector<double> a;
    for(const auto &p : r.profiles) a.push_back(p.area);
    std::sort(a.begin(), a.end());
    return a;
}

ProfileBuildResult build(const Sketch &s) { return buildProfiles(sketchCurves(s)); }

double totalArea(const ProfileBuildResult &r) {
    double t = 0;
    for(const auto &p : r.profiles) t += p.area;
    return t;
}

} // namespace

TEST_CASE("rectangle gives one profile") {
    Sketch s;
    s.addRectangle({0, 0}, {40, 20});
    auto r = build(s);
    REQUIRE(r.profiles.size() == 1);
    CHECK(r.profiles[0].area == doctest::Approx(800.0));
    CHECK(r.profiles[0].holes.empty());
    CHECK(r.profiles[0].outer.segs.size() == 4);
    CHECK(r.profiles[0].contains(r.profiles[0].sample));
}

TEST_CASE("circle inside a rectangle gives a ring and a disk") {
    Sketch s;
    s.addRectangle({0, 0}, {40, 20});
    s.addCircle(Vec2{20, 10}, 5);
    auto r = build(s);
    REQUIRE(r.profiles.size() == 2);
    const auto areas = sortedAreas(r);
    CHECK(areas[0] == doctest::Approx(kPi * 25).epsilon(1e-9));
    CHECK(areas[1] == doctest::Approx(800 - kPi * 25).epsilon(1e-9));
    const Profile &ring = r.profiles[0].holes.empty() ? r.profiles[1] : r.profiles[0];
    CHECK(ring.holes.size() == 1);
    CHECK(ring.contains({2, 2}));
    CHECK_FALSE(ring.contains({20, 10}));
    for(const auto &p : r.profiles) CHECK(p.contains(p.sample));
}

TEST_CASE("nested rectangles") {
    Sketch s;
    s.addRectangle({0, 0}, {100, 100});
    s.addRectangle({10, 10}, {90, 90});
    s.addRectangle({20, 20}, {80, 80});
    auto r = build(s);
    REQUIRE(r.profiles.size() == 3);
    // Inner square 60x60, middle ring 80^2 - 60^2, outer ring 100^2 - 80^2.
    const auto areas = sortedAreas(r);
    CHECK(areas[0] == doctest::Approx(6400 - 3600));
    CHECK(areas[1] == doctest::Approx(3600));
    CHECK(areas[2] == doctest::Approx(10000 - 6400));
    int withHoles = 0;
    for(const auto &p : r.profiles) withHoles += int(p.holes.size());
    CHECK(withHoles == 2);
}

TEST_CASE("two overlapping circles give three regions") {
    Sketch s;
    s.addCircle(Vec2{0, 0}, 10);
    s.addCircle(Vec2{12, 0}, 10);
    auto r = build(s);
    REQUIRE(r.profiles.size() == 3);
    // Lens area for two circles of radius R at distance d.
    const double R = 10, d = 12;
    const double lens = 2 * R * R * std::acos(d / (2 * R)) - 0.5 * d * std::sqrt(4 * R * R - d * d);
    const auto areas = sortedAreas(r);
    CHECK(areas[0] == doctest::Approx(lens).epsilon(1e-9));
    CHECK(areas[1] == doctest::Approx(kPi * R * R - lens).epsilon(1e-9));
    CHECK(areas[2] == doctest::Approx(kPi * R * R - lens).epsilon(1e-9));
    for(const auto &p : r.profiles) CHECK(p.contains(p.sample));
}

TEST_CASE("line crossing a circle splits it; outside pieces dangle") {
    Sketch s;
    s.addCircle(Vec2{0, 0}, 10);
    s.addLine(Vec2{-20, 0}, Vec2{20, 0});
    auto r = build(s);
    REQUIRE(r.profiles.size() == 2);
    const auto areas = sortedAreas(r);
    CHECK(areas[0] == doctest::Approx(kPi * 50).epsilon(1e-9));
    CHECK(areas[1] == doctest::Approx(kPi * 50).epsilon(1e-9));
    CHECK(r.danglingSegments == 2);
}

TEST_CASE("dangling lines and T junctions") {
    Sketch s;
    s.addRectangle({0, 0}, {40, 20});
    s.addLine(Vec2{40, 10}, Vec2{60, 10}); // tail sticking out (dangling)
    s.addLine(Vec2{20, 0}, Vec2{20, 20});  // splits the rectangle (T junctions)
    auto r = build(s);
    REQUIRE(r.profiles.size() == 2);
    const auto areas = sortedAreas(r);
    CHECK(areas[0] == doctest::Approx(400));
    CHECK(areas[1] == doctest::Approx(400));
}

TEST_CASE("open chain gives no profile") {
    Sketch s;
    s.addLine(Vec2{0, 0}, Vec2{10, 0});
    s.addLine(Vec2{10, 0}, Vec2{10, 10});
    auto r = build(s);
    CHECK(r.profiles.empty());
}

TEST_CASE("arc closed by a line (D shape)") {
    Sketch s;
    const int c = s.addPoint(0, 0);
    const int a = s.addPoint(10, 0);
    const int b = s.addPoint(-10, 0);
    s.addArc(c, a, b); // CCW upper half
    s.addLine(b, a);
    auto r = build(s);
    REQUIRE(r.profiles.size() == 1);
    CHECK(r.profiles[0].area == doctest::Approx(kPi * 50).epsilon(1e-9));
    CHECK(r.profiles[0].contains({0, 5}));
    CHECK_FALSE(r.profiles[0].contains({0, -5}));
}

TEST_CASE("slot: two arcs and two lines") {
    Sketch s;
    const int c1 = s.addPoint(0, 0), c2 = s.addPoint(30, 0);
    const int a1 = s.addPoint(0, -5), b1 = s.addPoint(0, 5);
    const int a2 = s.addPoint(30, -5), b2 = s.addPoint(30, 5);
    s.addArc(c1, b1, a1);  // left half, CCW from top to bottom
    s.addArc(c2, a2, b2);  // right half, CCW from bottom to top
    s.addLine(a1, a2);
    s.addLine(b2, b1);
    auto r = build(s);
    REQUIRE(r.profiles.size() == 1);
    CHECK(r.profiles[0].area == doctest::Approx(30 * 10 + kPi * 25).epsilon(1e-9));
}

TEST_CASE("circle tangent to a line is still a closed disk") {
    Sketch s;
    s.addCircle(Vec2{0, 10}, 10);
    s.addLine(Vec2{-20, 0}, Vec2{20, 0});
    auto r = build(s);
    REQUIRE(r.profiles.size() == 1);
    CHECK(r.profiles[0].area == doctest::Approx(kPi * 100).epsilon(1e-9));
}

TEST_CASE("construction geometry is ignored") {
    Sketch s;
    s.addRectangle({0, 0}, {40, 20});
    s.addLine(Vec2{20, 0}, Vec2{20, 20}, /*construction=*/true);
    auto r = build(s);
    CHECK(r.profiles.size() == 1);
}

TEST_CASE("profile keys are stable and ordered") {
    Sketch s;
    s.addRectangle({0, 0}, {40, 20});
    auto r1 = build(s);
    auto r2 = build(s);
    REQUIRE(r1.profiles.size() == 1);
    CHECK(r1.profiles[0].key == r2.profiles[0].key);
    CHECK(r1.profiles[0].key.find("c5.0") != std::string::npos);
}

TEST_CASE("total area of a grid of cells") {
    Sketch s;
    s.addRectangle({0, 0}, {30, 30});
    s.addLine(Vec2{10, -5}, Vec2{10, 35});
    s.addLine(Vec2{20, -5}, Vec2{20, 35});
    s.addLine(Vec2{-5, 10}, Vec2{35, 10});
    s.addLine(Vec2{-5, 20}, Vec2{35, 20});
    auto r = build(s);
    CHECK(r.profiles.size() == 9);
    CHECK(totalArea(r) == doctest::Approx(900));
}
