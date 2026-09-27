// Sketch Offset: chains, offset geometry joined at the corners, the
// constraints that keep it following the original, and too-big offsets.
#include <doctest.h>

#include "sketch/ProfileBuilder.h"
#include "sketch/Sketch.h"
#include "sketch/SketchOffset.h"
#include "sketch/SketchSolver.h"

#include <cmath>
#include <map>

using namespace cad;

namespace {

struct Values {
    std::map<std::string, double> v;
    DimensionLookup lookup() const {
        return [this](const std::string &n, double &out) {
            auto it = v.find(n);
            if(it == v.end()) return false;
            out = it->second;
            return true;
        };
    }
};

bool offset(Sketch &s, const std::vector<int> &curves, double outward, Values &vals, OffsetApplied &out,
            std::string *why = nullptr) {
    std::vector<CurveChain> chains;
    std::string error;
    if(!buildChains(s, curves, chains, error)) {
        if(why) *why = error;
        return false;
    }
    const double d = outward * outwardSign(s, chains.front());
    vals.v["off"] = std::fabs(outward);
    const bool ok = applyOffset(s, chains, d, "off", std::to_string(std::fabs(outward)) + " mm", vals.lookup(), out, error);
    if(why) *why = error;
    return ok;
}

// Bounding box of the given curves' points.
void box(const Sketch &s, const std::vector<int> &ids, Vec2 &lo, Vec2 &hi) {
    lo = {1e9, 1e9};
    hi = {-1e9, -1e9};
    for(int id : ids) {
        const SkEntity *e = s.find(id);
        if(!e || e->type != SkType::Line) continue;
        for(int p : {e->a, e->b}) {
            const Vec2 q = s.pointPos(p);
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
        }
    }
}

std::vector<int> curvesOf(const Sketch &s, const std::vector<int> &ids) {
    std::vector<int> out;
    for(int id : ids)
        if(s.find(id) && s.find(id)->isCurve()) out.push_back(id);
    return out;
}

} // namespace

TEST_CASE("offset: chains") {
    Sketch s;
    const auto rect = s.addRectangle({0, 0}, {20, 10});
    const int loose = s.addLine(Vec2{40, 0}, Vec2{50, 0});
    // One click on an edge picks the whole outline, not the loose line.
    auto picked = connectedCurves(s, rect[2]);
    CHECK(picked.size() == 4);
    CHECK(std::count(picked.begin(), picked.end(), loose) == 0);
    std::vector<CurveChain> chains;
    std::string error;
    REQUIRE(buildChains(s, picked, chains, error));
    REQUIRE(chains.size() == 1);
    CHECK(chains[0].closed);
    CHECK(chains[0].links.size() == 4);
    // A branch (a third line at a corner) is refused.
    const int spur = s.addLine(s.find(rect[0])->a, s.addPoint(-5, -5));
    auto withSpur = picked;
    withSpur.push_back(spur);
    CHECK_FALSE(buildChains(s, withSpur, chains, error));
    // ...and chain picking stops at it.
    CHECK(connectedCurves(s, rect[2]).size() < 5);
}

TEST_CASE("offset: rectangle outwards and inwards, following its dimension") {
    for(double d : {2.0, -2.0}) {
        CAPTURE(d);
        Sketch s;
        const auto rect = s.addRectangle({0, 0}, {20, 10});
        Values vals;
        OffsetApplied out;
        std::string why;
        REQUIRE_MESSAGE(offset(s, {rect.begin(), rect.end()}, d, vals, out, &why), why);
        const auto made = curvesOf(s, out.entities);
        REQUIRE(made.size() == 4);
        Vec2 lo, hi;
        box(s, made, lo, hi);
        CHECK(lo.x == doctest::Approx(-d));
        CHECK(lo.y == doctest::Approx(-d));
        CHECK(hi.x == doctest::Approx(20 + d));
        CHECK(hi.y == doctest::Approx(10 + d));
        // Two nested rectangles: the ring and the inner region.
        CHECK(buildProfiles(sketchCurves(s)).profiles.size() == 2);
        // One dimension drives all four sides.
        REQUIRE(out.dimension);
        CHECK(s.findConstraint(out.dimension)->param == "off");
        int following = 0;
        for(const auto &c : s.constraints) following += c.valueFrom == out.dimension;
        CHECK(following == 3);
        // Changing it moves every side (the original held still); so does
        // reshaping the original.
        const int fixA = s.addConstraint(SkCon::Fix, rect[0]), fixB = s.addConstraint(SkCon::Fix, rect[2]);
        vals.v["off"] = 3.0;
        REQUIRE(solveSketch(s, vals.lookup()).ok);
        box(s, made, lo, hi);
        const double d3 = d > 0 ? 3.0 : -3.0;
        CHECK(lo.x == doctest::Approx(-d3));
        CHECK(hi.y == doctest::Approx(10 + d3));
        s.removeConstraint(fixA);
        s.removeConstraint(fixB);
        vals.v["w"] = 30.0;
        s.addConstraint(SkCon::Distance, rect[0], 0, 0, "w");
        s.addConstraint(SkCon::Fix, s.find(rect[0])->a);
        REQUIRE(solveSketch(s, vals.lookup()).ok);
        box(s, made, lo, hi);
        CHECK(hi.x - lo.x == doctest::Approx(30 + 2 * d3));
        // Deleting the dimension frees the rest (no orphans left behind).
        s.removeConstraint(out.dimension);
        for(const auto &c : s.constraints) CHECK(c.valueFrom == 0);
    }
}

TEST_CASE("offset: a slot keeps its round ends and tangency") {
    // Two lines and two half-circle arcs, tangent where they meet.
    Sketch s;
    const int c1 = s.addPoint(0, 0), c2 = s.addPoint(20, 0);
    const int p1 = s.addPoint(0, -5), p2 = s.addPoint(20, -5), p3 = s.addPoint(20, 5), p4 = s.addPoint(0, 5);
    const int bottom = s.addLine(p1, p2), top = s.addLine(p3, p4);
    const int right = s.addArc(c2, p2, p3), left = s.addArc(c1, p4, p1);
    Values vals;
    OffsetApplied out;
    std::string why;
    REQUIRE_MESSAGE(offset(s, {bottom, right, top, left}, 1.5, vals, out, &why), why);
    const auto made = curvesOf(s, out.entities);
    REQUIRE(made.size() == 4);
    for(int id : made) {
        const SkEntity &e = *s.find(id);
        if(e.type == SkType::Arc) CHECK(s.arcRadius(e) == doctest::Approx(6.5));
    }
    CHECK(buildProfiles(sketchCurves(s)).profiles.size() == 2);
    // It follows the offset value, still round and tangent.
    for(int p : {c1, c2, p1, p2, p3, p4}) s.addConstraint(SkCon::Fix, p);
    vals.v["off"] = 2.5;
    const SolveOutcome r = solveSketch(s, vals.lookup());
    REQUIRE(r.ok);
    for(int id : made) {
        const SkEntity &e = *s.find(id);
        if(e.type == SkType::Arc) CHECK(s.arcRadius(e) == doctest::Approx(7.5));
        if(e.type == SkType::Line) CHECK(std::fabs(s.pointPos(e.a).y) == doctest::Approx(7.5));
    }
}

TEST_CASE("offset: circles and open chains") {
    SUBCASE("a lone circle") {
        Sketch s;
        const int c = s.addCircle(Vec2{5, 5}, 10);
        Values vals;
        OffsetApplied out;
        REQUIRE(offset(s, {c}, -3, vals, out));
        const auto made = curvesOf(s, out.entities);
        REQUIRE(made.size() == 1);
        CHECK(s.find(made[0])->r == doctest::Approx(7));
        CHECK((s.findConstraint(out.dimension)->type == SkCon::OffsetRadius));
        s.addConstraint(SkCon::Fix, c);
        vals.v["off"] = 4;
        const SolveOutcome r = solveSketch(s, vals.lookup());
        REQUIRE(r.ok);
        CHECK(s.find(made[0])->r == doctest::Approx(6));
        // Only the radius was free, and the offset pins it.
        CHECK(r.dof == 0);
    }
    SUBCASE("an open L") {
        Sketch s;
        const int corner = s.addPoint(10, 0);
        const int a = s.addLine(s.addPoint(0, 0), corner), b = s.addLine(corner, s.addPoint(10, 10));
        Values vals;
        OffsetApplied out;
        REQUIRE(offset(s, {a, b}, 1, vals, out)); // open: +1 is to the left of 0,0 -> 10,0 -> 10,10
        const auto made = curvesOf(s, out.entities);
        REQUIRE(made.size() == 2);
        // Left of the path is inside the L: the corner moves to (9, 1).
        const SkEntity &first = *s.find(made[0]);
        CHECK(s.pointPos(first.b).x == doctest::Approx(9));
        CHECK(s.pointPos(first.b).y == doctest::Approx(1));
        CHECK(s.pointPos(first.a).x == doctest::Approx(0));
    }
}

TEST_CASE("offset: too big") {
    Sketch s;
    const auto rect = s.addRectangle({0, 0}, {20, 4});
    Values vals;
    OffsetApplied out;
    std::string why;
    const size_t before = s.entities.size();
    CHECK_FALSE(offset(s, {rect.begin(), rect.end()}, -3, vals, out, &why));
    CHECK(why.find("too big") != std::string::npos);
    CHECK(s.entities.size() == before);
    Sketch c;
    const int circle = c.addCircle(Vec2{0, 0}, 2);
    CHECK_FALSE(offset(c, {circle}, -2.5, vals, out, &why));
}

TEST_CASE("offset: the value survives saving") {
    Sketch s;
    const auto rect = s.addRectangle({0, 0}, {20, 10});
    Values vals;
    OffsetApplied out;
    REQUIRE(offset(s, {rect.begin(), rect.end()}, 2, vals, out));
    const Sketch back = Sketch::fromJson(s.toJson());
    int following = 0;
    for(const auto &c : back.constraints) following += c.valueFrom == out.dimension;
    CHECK(following == 3);
}
