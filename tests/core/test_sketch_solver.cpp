// Sketch constraint solving through libslvs: every constraint type,
// over-constraint detection, degrees of freedom and free-entity analysis,
// and dimensions driving the 3D model through the timeline.
#include <doctest.h>

#include "TestModels.h"
#include "sketch/SketchSolver.h"

#include <cmath>
#include <map>
#include <set>

using namespace cadtest;

namespace {

constexpr double PI = 3.14159265358979323846;

struct Solver {
    std::map<std::string, double> values;
    SolveOutcome solve(Sketch &s, SolveOptions opt = {}) {
        return solveSketch(
            s,
            [&](const std::string &p, double &v) {
                auto it = values.find(p);
                if(it == values.end()) return false;
                v = it->second;
                return true;
            },
            opt);
    }
};

int dim(Sketch &s, Solver &sv, SkCon type, double value, int e1, int e2 = 0) {
    const std::string name = "p" + std::to_string(sv.values.size() + 1);
    sv.values[name] = value;
    return s.addConstraint(type, e1, e2, 0, name);
}

Vec2 P(const Sketch &s, int id) { return s.pointPos(id); }
const SkEntity &E(const Sketch &s, int id) { return *s.find(id); }
double len(const Sketch &s, int line) { return distance(P(s, E(s, line).a), P(s, E(s, line).b)); }

} // namespace

TEST_CASE("geometric constraints") {
    Solver sv;
    SUBCASE("coincident, horizontal, vertical") {
        Sketch s;
        const int l1 = s.addLine(Vec2{0, 0}, Vec2{10, 1});
        const int l2 = s.addLine(Vec2{10.5, 0.7}, Vec2{12, 9});
        s.addConstraint(SkCon::Coincident, E(s, l1).b, E(s, l2).a);
        s.addConstraint(SkCon::Horizontal, l1);
        s.addConstraint(SkCon::Vertical, l2);
        REQUIRE(sv.solve(s).ok);
        CHECK(distance(P(s, E(s, l1).b), P(s, E(s, l2).a)) < 1e-9);
        CHECK(std::fabs(P(s, E(s, l1).a).y - P(s, E(s, l1).b).y) < 1e-9);
        CHECK(std::fabs(P(s, E(s, l2).a).x - P(s, E(s, l2).b).x) < 1e-9);
    }
    SUBCASE("parallel, perpendicular, equal, midpoint") {
        Sketch s;
        const int a = s.addLine(Vec2{0, 0}, Vec2{10, 2});
        const int b = s.addLine(Vec2{0, 5}, Vec2{8, 9});
        const int c = s.addLine(Vec2{20, 0}, Vec2{22, 7});
        const int mid = s.addPoint(3, 3);
        s.addConstraint(SkCon::Parallel, a, b);
        s.addConstraint(SkCon::Perpendicular, a, c);
        s.addConstraint(SkCon::Equal, a, c);
        s.addConstraint(SkCon::Midpoint, mid, b);
        REQUIRE(sv.solve(s).ok);
        auto dir = [&](int l) { return (P(s, E(s, l).b) - P(s, E(s, l).a)).normalized(); };
        CHECK(std::fabs(dir(a).cross(dir(b))) < 1e-9);
        CHECK(std::fabs(dir(a).dot(dir(c))) < 1e-9);
        CHECK(len(s, a) == doctest::Approx(len(s, c)));
        CHECK(distance(P(s, mid), (P(s, E(s, b).a) + P(s, E(s, b).b)) * 0.5) < 1e-9);
    }
    SUBCASE("tangent line and arc sharing an endpoint") {
        Sketch s;
        const int c = s.addPoint(0, 0), st = s.addPoint(10, 0), en = s.addPoint(0, 10);
        const int arc = s.addArc(c, st, en);
        const int far = s.addPoint(12, -9);
        const int line = s.addLine(far, st);
        s.addConstraint(SkCon::Tangent, arc, line);
        REQUIRE(sv.solve(s).ok);
        const Vec2 radial = P(s, st) - P(s, c);
        const Vec2 along = P(s, st) - P(s, far);
        CHECK(std::fabs(radial.normalized().dot(along.normalized())) < 1e-8);
    }
    SUBCASE("tangent line and circle without a shared point") {
        Sketch s;
        const int circ = s.addCircle(Vec2{0, 0}, 5);
        const int line = s.addLine(Vec2{-10, 7}, Vec2{10, 6});
        s.addConstraint(SkCon::Tangent, line, circ);
        REQUIRE(sv.solve(s).ok);
        const Vec2 a = P(s, E(s, line).a), b = P(s, E(s, line).b), c = P(s, E(s, circ).a);
        const double d = std::fabs((b - a).cross(c - a)) / distance(a, b);
        CHECK(d == doctest::Approx(E(s, circ).r).epsilon(1e-8));
    }
    SUBCASE("tangent circles") {
        Sketch s;
        const int c1 = s.addCircle(Vec2{0, 0}, 5);
        const int c2 = s.addCircle(Vec2{12, 1}, 4);
        s.addConstraint(SkCon::Tangent, c1, c2);
        REQUIRE(sv.solve(s).ok);
        const double d = distance(P(s, E(s, c1).a), P(s, E(s, c2).a));
        CHECK(d == doctest::Approx(E(s, c1).r + E(s, c2).r).epsilon(1e-8));
    }
    SUBCASE("concentric, equal radius, symmetric, point on curves, origin and axes") {
        Sketch s;
        const int c1 = s.addCircle(Vec2{1, 1}, 5);
        const int c2 = s.addCircle(Vec2{2, 3}, 3);
        s.addConstraint(SkCon::Concentric, c1, c2);
        s.addConstraint(SkCon::Coincident, E(s, c1).a, kSketchOrigin);
        const int axisLine = s.addLine(Vec2{0, -20}, Vec2{0.5, 20});
        s.addConstraint(SkCon::Coincident, E(s, axisLine).a, kSketchYAxis);
        s.addConstraint(SkCon::Coincident, E(s, axisLine).b, kSketchYAxis);
        const int p1 = s.addPoint(-4, 7), p2 = s.addPoint(5, 8);
        s.addConstraint(SkCon::Symmetric, p1, p2, axisLine);
        const int onCircle = s.addPoint(9, 9);
        s.addConstraint(SkCon::PointOnCurve, onCircle, c1);
        const int onX = s.addPoint(3, 4);
        s.addConstraint(SkCon::PointOnCurve, onX, kSketchXAxis);
        const int c3 = s.addCircle(Vec2{30, 0}, 2);
        s.addConstraint(SkCon::Equal, c3, c1);
        REQUIRE(sv.solve(s).ok);
        CHECK(distance(P(s, E(s, c1).a), Vec2{0, 0}) < 1e-9);
        CHECK(distance(P(s, E(s, c2).a), Vec2{0, 0}) < 1e-9);
        CHECK(std::fabs(P(s, E(s, axisLine).a).x) < 1e-9);
        CHECK(std::fabs(P(s, p1).x + P(s, p2).x) < 1e-9);
        CHECK(std::fabs(P(s, p1).y - P(s, p2).y) < 1e-9);
        CHECK(distance(P(s, onCircle), P(s, E(s, c1).a)) == doctest::Approx(E(s, c1).r).epsilon(1e-9));
        CHECK(std::fabs(P(s, onX).y) < 1e-9);
        CHECK(E(s, c3).r == doctest::Approx(E(s, c1).r).epsilon(1e-9));
    }
    SUBCASE("fix keeps a point in place") {
        Sketch s;
        const int line = s.addLine(Vec2{3, 4}, Vec2{20, 9});
        s.addConstraint(SkCon::Fix, E(s, line).a);
        s.addConstraint(SkCon::Horizontal, line);
        REQUIRE(sv.solve(s).ok);
        CHECK(distance(P(s, E(s, line).a), Vec2{3, 4}) < 1e-9);
        CHECK(P(s, E(s, line).b).y == doctest::Approx(4));
    }
}

TEST_CASE("dimensions") {
    Solver sv;
    Sketch s;
    const auto rect = s.addRectangle({0, 0}, {37, 18});
    s.addConstraint(SkCon::Coincident, E(s, rect[0]).a, kSketchOrigin);
    dim(s, sv, SkCon::Distance, 40.0, rect[0]);         // width
    dim(s, sv, SkCon::VDistance, 25.0, E(s, rect[1]).a, E(s, rect[1]).b); // height
    const int circ = s.addCircle(Vec2{15, 10}, 3);
    dim(s, sv, SkCon::Diameter, 8.0, circ);
    dim(s, sv, SkCon::HDistance, 12.0, kSketchOrigin, E(s, circ).a);
    dim(s, sv, SkCon::PointLineDistance, 9.0, E(s, circ).a, rect[0]);
    const int arcC = s.addPoint(60, 0), arcS = s.addPoint(66, 0), arcE = s.addPoint(60, 6);
    const int arc = s.addArc(arcC, arcS, arcE);
    dim(s, sv, SkCon::Radius, 10.0, arc);
    const int l1 = s.addLine(Vec2{80, 0}, Vec2{90, 0});
    const int l2 = s.addLine(Vec2{80, 0}, Vec2{88, 5});
    s.addConstraint(SkCon::Coincident, E(s, l1).a, E(s, l2).a);
    s.addConstraint(SkCon::Horizontal, l1);
    dim(s, sv, SkCon::Angle, 30.0 * PI / 180.0, l1, l2);
    const auto r = sv.solve(s);
    REQUIRE_MESSAGE(r.ok, r.message);
    CHECK(len(s, rect[0]) == doctest::Approx(40));
    CHECK(len(s, rect[1]) == doctest::Approx(25));
    CHECK(E(s, circ).r == doctest::Approx(4));
    CHECK(P(s, E(s, circ).a).x == doctest::Approx(12));
    CHECK(P(s, E(s, circ).a).y == doctest::Approx(9));
    CHECK(s.arcRadius(E(s, arc)) == doctest::Approx(10));
    CHECK(distance(P(s, arcC), P(s, arcE)) == doctest::Approx(10));
    const Vec2 d1 = (P(s, E(s, l1).b) - P(s, E(s, l1).a)).normalized();
    const Vec2 d2 = (P(s, E(s, l2).b) - P(s, E(s, l2).a)).normalized();
    CHECK(std::acos(std::clamp(d1.dot(d2), -1.0, 1.0)) == doctest::Approx(PI / 6));
}

TEST_CASE("points on lines stay where they are") {
    // Regression for the vendored libslvs: PT_ON_LINE's line parameter used to
    // start at 0, pulling a point that already lay on the line halfway to its start.
    Solver sv;
    for(const int axis : {kSketchXAxis, kSketchYAxis}) {
        Sketch s;
        const Vec2 at = axis == kSketchXAxis ? Vec2(20, 0) : Vec2(0, 20);
        const int p = s.addPoint(at.x, at.y);
        s.addConstraint(SkCon::PointOnCurve, p, axis);
        REQUIRE(sv.solve(s).ok);
        CHECK(distance(P(s, p), at) < 1e-9);
    }
    Sketch s;
    const int l = s.addLine(Vec2{0, 0}, Vec2{40, 20});
    const int p = s.addPoint(30, 15);
    s.addConstraint(SkCon::PointOnCurve, p, l);
    REQUIRE(sv.solve(s).ok);
    CHECK(distance(P(s, p), Vec2(30, 15)) < 1e-9);
    CHECK(distance(P(s, E(s, l).a), Vec2(0, 0)) < 1e-9);
    // A point off the line moves onto it, near where it was.
    s.find(p)->y = 17;
    REQUIRE(sv.solve(s).ok);
    const Vec2 d = (P(s, E(s, l).b) - P(s, E(s, l).a)).normalized();
    CHECK(std::fabs(d.cross(P(s, p) - P(s, E(s, l).a))) < 1e-9);
    CHECK(distance(P(s, p), Vec2(30, 16)) < 2.0);
}

TEST_CASE("free-entity analysis treats separate shapes independently") {
    Solver sv;
    Sketch s;
    const auto rect = s.addRectangle({0, 0}, {40, 20});
    s.addConstraint(SkCon::Coincident, E(s, rect[0]).a, kSketchOrigin);
    dim(s, sv, SkCon::Distance, 40.0, rect[0]);
    dim(s, sv, SkCon::Distance, 20.0, rect[1]);
    const int circle = s.addCircle(Vec2{80, 10}, 5);
    const int lone = s.addLine(Vec2{0, 40}, Vec2{30, 50});
    s.addConstraint(SkCon::Horizontal, lone);
    const auto r = sv.solve(s);
    REQUIRE(r.ok);
    CHECK(r.dof == 3 + 3);
    const std::set<int> isFree(r.freeEntities.begin(), r.freeEntities.end());
    CHECK(isFree.count(circle));
    CHECK(isFree.count(E(s, circle).a));
    CHECK(isFree.count(lone));
    for(int l : rect) CHECK_FALSE(isFree.count(l));
    for(int l : rect) CHECK_FALSE(isFree.count(E(s, l).a));
}

TEST_CASE("over-constrained sketches are rejected without moving geometry") {
    Solver sv;
    Sketch s;
    const auto rect = s.addRectangle({0, 0}, {40, 20});
    const int w1 = dim(s, sv, SkCon::Distance, 40.0, rect[0]);
    const int w2 = dim(s, sv, SkCon::Distance, 30.0, rect[2]); // opposite side, inconsistent
    const Sketch before = s;
    const auto r = sv.solve(s);
    CHECK_FALSE(r.ok);
    CHECK(r.message.find("over-constrained") != std::string::npos);
    CHECK((std::find(r.failed.begin(), r.failed.end(), w1) != r.failed.end() ||
           std::find(r.failed.begin(), r.failed.end(), w2) != r.failed.end()));
    CHECK(s.toJson() == before.toJson());
}

TEST_CASE("degrees of freedom and fully constrained geometry") {
    Solver sv;
    Sketch s;
    const auto rect = s.addRectangle({0, 0}, {40, 20});
    auto r = sv.solve(s);
    REQUIRE(r.ok);
    CHECK(r.dof == 4); // position x, y, width, height
    CHECK(r.freeEntities.size() == 8);

    dim(s, sv, SkCon::Distance, 40, rect[0]);
    dim(s, sv, SkCon::Distance, 20, rect[1]);
    r = sv.solve(s);
    CHECK(r.dof == 2);
    CHECK(r.freeEntities.size() == 8); // still floating

    s.addConstraint(SkCon::Coincident, E(s, rect[0]).a, kSketchOrigin);
    r = sv.solve(s);
    CHECK(r.dof == 0);
    CHECK(r.freeEntities.empty());

    // A circle: centre (2) + radius (1).
    const int circ = s.addCircle(Vec2{10, 10}, 3);
    r = sv.solve(s);
    CHECK(r.dof == 3);
    CHECK(r.freeEntities.size() == 2); // the circle and its centre point
    dim(s, sv, SkCon::Diameter, 6, circ);
    r = sv.solve(s);
    CHECK(r.dof == 2);
    s.addConstraint(SkCon::HDistance, kSketchOrigin, E(s, circ).a, 0, "hx");
    sv.values["hx"] = 10;
    s.addConstraint(SkCon::VDistance, kSketchOrigin, E(s, circ).a, 0, "hy");
    sv.values["hy"] = 10;
    r = sv.solve(s);
    CHECK(r.dof == 0);
    CHECK(r.freeEntities.empty());
}

TEST_CASE("dragging moves under-constrained geometry toward the cursor") {
    Solver sv;
    Sketch s;
    const auto rect = s.addRectangle({0, 0}, {40, 20});
    s.addConstraint(SkCon::Coincident, E(s, rect[0]).a, kSketchOrigin);
    const int corner = E(s, rect[1]).b; // top-right
    s.find(corner)->x = 55;
    s.find(corner)->y = 30;
    SolveOptions opt;
    opt.dragged = {corner};
    REQUIRE(sv.solve(s, opt).ok);
    CHECK(P(s, corner).x == doctest::Approx(55));
    CHECK(P(s, corner).y == doctest::Approx(30));
    CHECK(len(s, rect[0]) == doctest::Approx(55));
}

TEST_CASE("sketch dimensions drive the model through parameters") {
    Document doc;
    auto sk = std::make_shared<SketchFeature>();
    sk->plane = PlaneRef::origin(PlaneRef::Kind::XY);
    const auto rect = sk->sketch.addRectangle({0, 0}, {33, 17});
    sk->sketch.addConstraint(SkCon::Coincident, sk->sketch.find(rect[0])->a, kSketchOrigin);
    const std::string w = doc.allocateParamName(), h = doc.allocateParamName();
    const int wc = sk->sketch.addConstraint(SkCon::Distance, rect[0], 0, 0, w);
    sk->sketch.findConstraint(wc)->expr = "40 mm";
    const int hc = sk->sketch.addConstraint(SkCon::Distance, rect[1], 0, 0, h);
    sk->sketch.findConstraint(hc)->expr = w + " / 2";
    const FeatureId sid = doc.addFeature(sk);
    doc.addFeature(extrudeAll(doc, sid, "10"));
    REQUIRE(doc.statusOf(sid).isOk());
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(40 * 20 * 10));
    CHECK(doc.displayedState()->sketches.at(sid)->dof == 0);

    // Change the width dimension: the height follows (w / 2) and so does the solid.
    auto edited = std::static_pointer_cast<SketchFeature>(doc.feature(sid)->clone());
    edited->sketch.findConstraint(wc)->expr = "60 mm";
    doc.replaceFeature(edited);
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(60 * 30 * 10));
    CHECK(doc.params().find(h)->value == doctest::Approx(30));
}
