// Sketch Trim, Extend, corner Fillet, Slot and regular Polygon.
#include <doctest.h>

#include "TestModels.h"
#include "sketch/CurveIntersect.h"
#include "sketch/SketchEdit.h"
#include "sketch/SketchSolver.h"
#include "sketch/SketchText.h"

#include <cmath>

using namespace cadtest;

namespace {

bool solve(Sketch &s) {
    return solveSketch(s, [](const std::string &, double &) { return false; }).ok;
}

int countType(const Sketch &s, SkType t) {
    int n = 0;
    for(const auto &e : s.entities) n += e.type == t;
    return n;
}

double lineLength(const Sketch &s, int id) {
    const SkEntity *l = s.find(id);
    return distance(s.pointPos(l->a), s.pointPos(l->b));
}

double totalArea(const Sketch &s) {
    double a = 0;
    for(const auto &p : sketchProfiles(s)) a += std::abs(p.area);
    return a;
}

} // namespace

TEST_CASE("curve intersection: shared helpers find crossings and parameters") {
    Sketch s;
    const int l = s.addLine(Vec2(-10, 0), Vec2(10, 0));
    const int c = s.addCircle(Vec2(0, 0), 5);
    std::vector<CurveHit> ha, hb;
    intersectCurves(curveOfEntity(s, *s.find(l)), curveOfEntity(s, *s.find(c)), 1e-9, ha, hb);
    REQUIRE(ha.size() == 2);
    CHECK(ha[0].param == doctest::Approx(0.25).epsilon(1e-9));
    CHECK(ha[1].param == doctest::Approx(0.75).epsilon(1e-9));
    const Curve2 line = curveOfEntity(s, *s.find(l));
    CHECK(curveParam(line, {5, 3}) == doctest::Approx(0.75));
    CHECK(distance(curvePoint(line, 0.5), {0, 0}) < 1e-12);
}

TEST_CASE("trim: the middle of a line crossed twice splits it in two") {
    Sketch s;
    const int h = s.addLine(Vec2(0, 0), Vec2(30, 0));
    s.addConstraint(SkCon::Horizontal, h);
    s.addLine(Vec2(10, -5), Vec2(10, 5));
    s.addLine(Vec2(20, -5), Vec2(20, 5));
    std::vector<int> made;
    std::string why;
    bool whole = true;
    const auto piece = trimPreview(s, h, {15, 0.2}, whole);
    CHECK_FALSE(whole);
    REQUIRE(piece.size() == 2);
    CHECK(distance(piece.front(), {10, 0}) < 1e-9);
    CHECK(distance(piece.back(), {20, 0}) < 1e-9);
    REQUIRE(trimCurve(s, h, {15, 0.2}, made, why));
    CHECK(countType(s, SkType::Line) == 4);
    CHECK(lineLength(s, h) == doctest::Approx(10.0));
    // The new piece is horizontal too, and both are held on the cutting lines.
    int horizontals = 0, onCurve = 0;
    for(const auto &c : s.constraints) {
        horizontals += c.type == SkCon::Horizontal;
        onCurve += c.type == SkCon::PointOnCurve;
    }
    CHECK(horizontals == 2);
    CHECK(onCurve == 2);
    REQUIRE(solve(s));
    CHECK(lineLength(s, h) == doctest::Approx(10.0));
}

TEST_CASE("trim: an end piece shortens the line; nothing crossing deletes it") {
    Sketch s;
    const int h = s.addLine(Vec2(0, 0), Vec2(30, 0));
    s.addConstraint(SkCon::Distance, h, 0, 0, "len");
    s.addLine(Vec2(10, -5), Vec2(10, 5));
    const int lone = s.addLine(Vec2(0, 20), Vec2(10, 20));
    std::vector<int> made;
    std::string why;
    REQUIRE(trimCurve(s, h, {25, 0}, made, why));
    CHECK(lineLength(s, h) == doctest::Approx(10.0));
    // Its length dimension goes: it is no longer that long.
    for(const auto &c : s.constraints) CHECK_FALSE((c.type == SkCon::Distance && c.e1 == h));
    REQUIRE(trimCurve(s, lone, {5, 20}, made, why));
    CHECK_FALSE(s.find(lone));
    CHECK_FALSE(trimCurve(s, 9999, {0, 0}, made, why));
}

TEST_CASE("trim: a circle crossed by a line becomes an arc; arcs split") {
    Sketch s;
    const int c = s.addCircle(Vec2(0, 0), 10);
    s.addConstraint(SkCon::Radius, c, 0, 0, "r");
    const int line = s.addLine(Vec2(-20, 0), Vec2(20, 0));
    std::vector<int> made;
    std::string why;
    REQUIRE(trimCurve(s, c, {0, -10}, made, why)); // the lower half goes
    const SkEntity *arc = s.find(c);
    REQUIRE(arc->type == SkType::Arc);
    CHECK(distance(s.pointPos(arc->b), {10, 0}) < 1e-9);
    CHECK(distance(s.pointPos(arc->c), {-10, 0}) < 1e-9);
    // The arc's ends are the line's crossing points, held on the line.
    REQUIRE(solveSketch(s, [](const std::string &n, double &v) {
                v = 10;
                return n == "r";
            }).ok);
    // Trim the line's ends off: one closed half disc is left.
    REQUIRE(trimCurve(s, line, {-15, 0}, made, why));
    std::vector<int> made2;
    int right = 0;
    for(const auto &e : s.entities)
        if(e.type == SkType::Line) right = e.id;
    REQUIRE(trimCurve(s, right, {15, 0}, made2, why));
    REQUIRE(sketchProfiles(s).size() == 1);
    CHECK(totalArea(s) == doctest::Approx(kPi * 100 / 2).epsilon(1e-3));

    // An arc crossed twice splits into two arcs of equal radius.
    Sketch t;
    const int ctr = t.addPoint(0, 0), st = t.addPoint(10, 0), en = t.addPoint(-10, 0);
    const int a = t.addArc(ctr, st, en);
    t.addLine(Vec2(3, -2), Vec2(3, 12));
    t.addLine(Vec2(-3, -2), Vec2(-3, 12));
    REQUIRE(trimCurve(t, a, {0, 10}, made, why));
    CHECK(countType(t, SkType::Arc) == 2);
    REQUIRE(solve(t));
}

TEST_CASE("extend: a line reaches the next line; an arc reaches a line") {
    Sketch s;
    const int l = s.addLine(Vec2(0, 0), Vec2(10, 0));
    const int wall = s.addLine(Vec2(25, -5), Vec2(25, 5));
    std::string why;
    REQUIRE(extendPreview(s, l, {9, 0}).size() == 2);
    REQUIRE(extendCurve(s, l, {9, 0}, why));
    CHECK(lineLength(s, l) == doctest::Approx(25.0));
    REQUIRE(solve(s));
    CHECK(lineLength(s, l) == doctest::Approx(25.0));
    // Joined now: the far end of the wall is not free to extend from there.
    CHECK_FALSE(extendCurve(s, l, {0, 0}, why)); // nothing behind it
    (void)wall;

    Sketch t;
    const int c = t.addPoint(0, 0), a0 = t.addPoint(10, 0), a1 = t.addPoint(0, 10);
    const int arc = t.addArc(c, a0, a1);
    t.addLine(Vec2(-20, -5), Vec2(20, -5)); // below: the arc's start runs CW down to it
    REQUIRE(extendCurve(t, arc, {9.5, 1}, why));
    CHECK(t.pointPos(t.find(arc)->b).y == doctest::Approx(-5.0));
    CHECK(distance(t.pointPos(t.find(arc)->b), {0, 0}) == doctest::Approx(10.0));
}

TEST_CASE("fillet: a rectangle corner becomes a tangent arc and keeps its dimensions") {
    Sketch s;
    const auto lines = s.addRectangle({0, 0}, {40, 20});
    s.addConstraint(SkCon::Distance, lines[0], 0, 0, "w");
    s.addConstraint(SkCon::Distance, lines[1], 0, 0, "h");
    s.addConstraint(SkCon::Fix, s.find(lines[0])->a);
    const int corner = filletCornerNear(s, {39, 19}, 3);
    REQUIRE(corner);
    CHECK(s.pointPos(corner).x == doctest::Approx(40));
    int arc = 0;
    std::vector<int> made;
    std::string why;
    CHECK_FALSE(filletCorner(s, corner, 25, arc, made, why)); // too big
    REQUIRE(filletCorner(s, corner, 5, arc, made, why));
    CHECK(countType(s, SkType::Arc) == 1);
    s.addConstraint(SkCon::Radius, arc, 0, 0, "r"); // as the Fillet tool does
    auto lookup = [](double w, double h) {
        return [w, h](const std::string &n, double &v) {
            if(n == "w") v = w;
            else if(n == "h") v = h;
            else if(n == "r") v = 5;
            else return false;
            return true;
        };
    };
    REQUIRE(solveSketch(s, lookup(40, 20)).ok);
    REQUIRE(sketchProfiles(s).size() == 1);
    CHECK(totalArea(s) == doctest::Approx(800 - 25 * (1 - kPi / 4)).epsilon(1e-6));
    // The width still measures to the corner, so changing it moves it.
    REQUIRE(solveSketch(s, lookup(50, 20)).ok);
    CHECK(totalArea(s) == doctest::Approx(1000 - 25 * (1 - kPi / 4)).epsilon(1e-6));
    CHECK(s.arcRadius(*s.find(arc)) == doctest::Approx(5.0));
    // A corner of an arc is not a line corner.
    CHECK(filletCornerNear(s, s.pointPos(s.find(arc)->b), 1) == 0);
}

TEST_CASE("slot: two half circles and two lines around a centre line") {
    Sketch s;
    const int c1 = s.addPoint(0, 0), c2 = s.addPoint(30, 0);
    SlotIds ids;
    std::string why;
    CHECK_FALSE(addSlot(s, c1, c1, 8, ids, why));
    REQUIRE(addSlot(s, c1, c2, 8, ids, why));
    CHECK(s.find(ids.centreLine)->construction);
    REQUIRE(solve(s));
    REQUIRE(sketchProfiles(s).size() == 1);
    CHECK(totalArea(s) == doctest::Approx(30 * 8 + kPi * 16).epsilon(1e-6));
    // Its radius drives both ends.
    s.addConstraint(SkCon::Diameter, ids.arc1, 0, 0, "w");
    REQUIRE(solveSketch(s, [](const std::string &n, double &v) {
                v = 10;
                return n == "w";
            }).ok);
    CHECK(s.arcRadius(*s.find(ids.arc2)) == doctest::Approx(5.0));
    CHECK(totalArea(s) == doctest::Approx(30 * 10 + kPi * 25).epsilon(1e-4));
}

TEST_CASE("polygon: a hexagon inscribed, or across flats for a nut") {
    Sketch s;
    const int c = s.addPoint(0, 0);
    PolygonIds ids;
    std::string why;
    REQUIRE(addRegularPolygon(s, c, {10, 0}, 6, true, ids, why));
    CHECK(ids.lines.size() == 6);
    CHECK(s.find(ids.circle)->construction);
    REQUIRE(solve(s));
    // Inscribed in r = 10: area 3 sqrt(3) / 2 r^2.
    CHECK(totalArea(s) == doctest::Approx(1.5 * std::sqrt(3.0) * 100).epsilon(1e-6));

    Sketch t;
    const int o = t.addPoint(0, 0);
    REQUIRE(addRegularPolygon(t, o, {0, 5.5}, 6, false, ids, why)); // M3 nut: 5.5 across flats
    t.addConstraint(SkCon::Diameter, ids.circle, 0, 0, "af");
    REQUIRE(solveSketch(t, [](const std::string &n, double &v) {
                v = 11;
                return n == "af";
            }).ok);
    // Across flats 11 mm: area 2 sqrt(3) a^2 with apothem a = 5.5.
    CHECK(totalArea(t) == doctest::Approx(2 * std::sqrt(3.0) * 5.5 * 5.5).epsilon(1e-6));
    CHECK_FALSE(addRegularPolygon(t, o, {0, 5}, 2, true, ids, why));
    CHECK(regularPolygonCorners({0, 0}, {1, 0}, 4, true).size() == 4);
}
