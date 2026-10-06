// Sketch mirror, circular pattern and projection: the geometry they make,
// that it stays linked (symmetric copies, projected lines following their
// source), and that construction geometry makes no profiles.
#include <doctest.h>

#include "TestModels.h"
#include "sketch/SketchOps.h"
#include "sketch/SketchText.h"
#include "sketch/SketchProject.h"
#include "sketch/SketchSolver.h"

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

bool near(Vec2 a, Vec2 b, double tol = 1e-6) { return distance(a, b) < tol; }

} // namespace

TEST_CASE("mirror: a half profile up to the mirror line becomes one closed profile") {
    Sketch s;
    // A U opening onto the Y axis: (0,0) -> (10,0) -> (10,5) -> (0,5).
    const int p0 = s.addPoint(0, 0), p1 = s.addPoint(10, 0), p2 = s.addPoint(10, 5), p3 = s.addPoint(0, 5);
    const int l1 = s.addLine(p0, p1), l2 = s.addLine(p1, p2), l3 = s.addLine(p2, p3);
    CHECK(sketchProfiles(s).empty());
    std::vector<int> made;
    std::string why;
    REQUIRE(mirrorEntities(s, {l1, l2, l3}, kSketchYAxis, made, why));
    CHECK(countType(s, SkType::Line) == 6);
    CHECK(countType(s, SkType::Point) == 6); // the two on the axis are shared
    REQUIRE(solve(s));
    const auto profiles = sketchProfiles(s);
    REQUIRE(profiles.size() == 1);
    CHECK(std::abs(profiles[0].outer.area) == doctest::Approx(100.0));
    // The mirror image follows the original.
    s.find(p1)->x = 14;
    s.find(p2)->x = 14;
    REQUIRE(solveSketch(s, [](const std::string &, double &) { return false; }, {{p1, p2}, false}).ok);
    double minX = 0, maxX = 0;
    for(const auto &e : s.entities)
        if(e.type == SkType::Point) minX = std::min(minX, e.x), maxX = std::max(maxX, e.x);
    CHECK(maxX == doctest::Approx(14.0).epsilon(0.01));
    CHECK(minX == doctest::Approx(-maxX));
    // Nothing selected, or no mirror line: errors.
    CHECK_FALSE(mirrorEntities(s, {}, kSketchYAxis, made, why));
    CHECK_FALSE(mirrorEntities(s, {l1}, p0, made, why));
}

TEST_CASE("mirror: circles keep their radius, arcs turn round, about any line") {
    Sketch s;
    const int a = s.addPoint(0, -10), b = s.addPoint(0, 10);
    const int axis = s.addLine(a, b, true); // a construction line as the mirror
    const int circle = s.addCircle(Vec2(6, 2), 3);
    const int c = s.addPoint(6, -5), st = s.addPoint(9, -5), en = s.addPoint(6, -2);
    const int arc = s.addArc(c, st, en);
    std::vector<int> made;
    std::string why;
    REQUIRE(mirrorEntities(s, {circle, arc}, axis, made, why));
    REQUIRE(solve(s));
    const SkEntity *mc = nullptr, *ma = nullptr;
    for(int id : made) {
        const SkEntity *e = s.find(id);
        if(e->type == SkType::Circle) mc = e;
        if(e->type == SkType::Arc) ma = e;
    }
    REQUIRE((mc && ma));
    CHECK(near(s.pointPos(mc->a), {-6, 2}));
    CHECK(mc->r == doctest::Approx(3.0));
    // The mirrored arc runs counter-clockwise from (-6,-2) to (-9,-5).
    CHECK(near(s.pointPos(ma->b), {-6, -2}));
    CHECK(near(s.pointPos(ma->c), {-9, -5}));
    // Changing the original circle's radius changes the copy's.
    s.addConstraint(SkCon::Radius, circle, 0, 0, "r");
    REQUIRE(solveSketch(s, [](const std::string &n, double &v) {
                v = 4.5;
                return n == "r";
            }).ok);
    CHECK(s.find(mc->id)->r == doctest::Approx(4.5));
}

TEST_CASE("circular pattern: evenly spread copies around a centre") {
    Sketch s;
    const int circle = s.addCircle(Vec2(20, 0), 3);
    std::vector<int> made;
    std::string why;
    REQUIRE(patternEntities(s, {circle}, {0, 0}, 6, 360, made, why));
    CHECK(countType(s, SkType::Circle) == 6);
    REQUIRE(solve(s));
    CHECK(sketchProfiles(s).size() == 6);
    bool found60 = false;
    for(const auto &e : s.entities)
        if(e.type == SkType::Circle && near(s.pointPos(e.a), {10, 10 * std::sqrt(3.0)}, 1e-6)) found60 = true;
    CHECK(found60);
    // Over a part of a turn, the copies span it end to end.
    CHECK(patternStep(4, 90) == doctest::Approx(30.0));
    CHECK(patternStep(4, 360) == doctest::Approx(90.0));
    CHECK_FALSE(patternEntities(s, {circle}, {0, 0}, 1, 360, made, why));
    CHECK_FALSE(patternEntities(s, {}, {0, 0}, 3, 360, made, why));
}

TEST_CASE("circular pattern: constraints come along, dimensions follow the original's") {
    Sketch s;
    const auto lines = s.addRectangle({10, -2}, {16, 2}); // horizontal / vertical constraints
    const int w = s.addConstraint(SkCon::Distance, lines[0], 0, 0, "w");
    std::vector<int> made;
    std::string why;
    REQUIRE(patternEntities(s, {lines.begin(), lines.end()}, {0, 0}, 4, 360, made, why));
    auto lookup = [](double width) {
        return [width](const std::string &n, double &v) {
            v = width;
            return n == "w";
        };
    };
    REQUIRE(solveSketch(s, lookup(6.0)).ok);
    CHECK(sketchProfiles(s).size() == 4);
    int follow = 0;
    for(const auto &c : s.constraints) follow += c.valueFrom == w;
    CHECK(follow == 3);
    // Every copy's width follows the dimension.
    REQUIRE(solveSketch(s, lookup(8.0)).ok);
    for(const auto &p : sketchProfiles(s)) CHECK(std::abs(p.outer.area) == doctest::Approx(32.0));
    // A turn that is not a quarter one drops horizontal / vertical.
    Sketch t;
    const auto r = t.addRectangle({10, -2}, {16, 2});
    REQUIRE(patternEntities(t, {r.begin(), r.end()}, {0, 0}, 3, 360, made, why));
    REQUIRE(solve(t));
    CHECK(sketchProfiles(t).size() == 3);
}

TEST_CASE("projection: lines follow a sketch on another plane; circles need a parallel plane") {
    Document doc;
    const FeatureId base = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {20, 10}));
    auto top = std::make_shared<SketchFeature>();
    top->plane = PlaneRef::origin(PlaneRef::Kind::XZ);
    const FeatureId side = doc.addFeature(top);
    const StatePtr st = doc.stateAt(doc.indexOf(side) + 1);
    const SketchResult &src = *st->sketches.at(base);
    const SketchResult &dst = *st->sketches.at(side);

    // The rectangle's bottom edge (along X, at y = 0) projects onto XZ as a line on its X axis.
    auto edited = std::static_pointer_cast<SketchFeature>(doc.feature(side)->clone());
    int bottom = 0;
    for(const auto &e : src.sketch.entities)
        if(e.type == SkType::Line && std::abs(src.sketch.pointPos(e.a).y) < 1e-9 && std::abs(src.sketch.pointPos(e.b).y) < 1e-9)
            bottom = e.id;
    REQUIRE(bottom);
    std::vector<int> made;
    std::string why;
    REQUIRE(projectEntity(edited->sketch, dst.frame, src, bottom, made, why));
    CHECK(made.size() == 3);
    // Again: nothing new.
    REQUIRE(projectEntity(edited->sketch, dst.frame, src, bottom, made, why));
    CHECK(made.empty());
    int projectedLine = 0;
    for(const auto &e : edited->sketch.entities)
        if(e.type == SkType::Line && e.isProjected()) projectedLine = e.id;
    REQUIRE(projectedLine);
    doc.replaceFeature(edited);
    CHECK((doc.feature(side)->dependencies() == std::vector<FeatureId>{base}));

    auto lineLength = [&](const StatePtr &state) {
        const SketchResult &r = *state->sketches.at(side);
        const SkEntity &l = *r.sketch.find(projectedLine);
        return distance(r.sketch.pointPos(l.a), r.sketch.pointPos(l.b));
    };
    CHECK(lineLength(doc.stateAt(doc.indexOf(side) + 1)) == doctest::Approx(20.0));
    // Fixed: it adds no degrees of freedom.
    CHECK(doc.stateAt(doc.indexOf(side) + 1)->sketches.at(side)->dof == 0);

    // Make the base rectangle wider: the projected line follows.
    auto wider = std::static_pointer_cast<SketchFeature>(doc.feature(base)->clone());
    for(auto &e : wider->sketch.entities)
        if(e.type == SkType::Point && e.x > 10) e.x = 30;
    doc.replaceFeature(wider);
    CHECK(lineLength(doc.stateAt(doc.indexOf(side) + 1)) == doctest::Approx(30.0));
    // It is a normal line: as construction it makes no profile, and back.
    CHECK(Sketch::fromJson(edited->sketch.toJson()).find(projectedLine)->projSketch == base);

    // A circle cannot be projected onto the tilted (perpendicular) plane...
    auto withCircle = std::static_pointer_cast<SketchFeature>(doc.feature(base)->clone());
    const int circle = withCircle->sketch.addCircle(Vec2(5, 5), 2);
    doc.replaceFeature(withCircle);
    const StatePtr st2 = doc.stateAt(doc.indexOf(side) + 1);
    Sketch onSide = std::static_pointer_cast<const SketchFeature>(doc.feature(side))->sketch;
    CHECK_FALSE(projectEntity(onSide, st2->sketches.at(side)->frame, *st2->sketches.at(base), circle, made, why));
    // ...but onto a parallel one, with its radius.
    auto flat = std::make_shared<SketchFeature>();
    flat->plane = PlaneRef::origin(PlaneRef::Kind::XY);
    const FeatureId above = doc.addFeature(flat);
    const StatePtr st3 = doc.stateAt(doc.indexOf(above) + 1);
    auto flatEdit = std::static_pointer_cast<SketchFeature>(doc.feature(above)->clone());
    REQUIRE(projectEntity(flatEdit->sketch, st3->sketches.at(above)->frame, *st3->sketches.at(base), circle, made, why));
    doc.replaceFeature(flatEdit);
    const SketchResult &r = *doc.stateAt(doc.indexOf(above) + 1)->sketches.at(above);
    CHECK(r.profiles.size() == 1);
    // Deleting the source leaves the projection where it was, with a warning.
    doc.deleteFeature(base);
    const SketchResult &orphan = *doc.stateAt(doc.indexOf(above) + 1)->sketches.at(above);
    CHECK(orphan.status.severity == Severity::Warning);
    CHECK(orphan.profiles.size() == 1);
}

TEST_CASE("construction geometry makes no profiles, and switching back restores them") {
    Sketch s;
    const auto lines = s.addRectangle({0, 0}, {10, 10});
    REQUIRE(sketchProfiles(s).size() == 1);
    for(int id : lines) s.find(id)->construction = true;
    CHECK(sketchProfiles(s).empty());
    for(int id : lines) s.find(id)->construction = false;
    CHECK(sketchProfiles(s).size() == 1);
}
