// Extrude taper angles and the To Object extent.
#include <doctest.h>

#include "TestModels.h"

#include <BRepCheck_Analyzer.hxx>

using namespace cadtest;

namespace {

constexpr double PI = 3.14159265358979323846;

// Volume of a square frustum with base side a, top side b and height h.
double frustum(double a, double b, double h) { return h / 3.0 * (a * a + b * b + a * b); }

double faceArea(const Body &b, gp_Dir n, double z) {
    for(int i = 1; i <= b.shape.faceCount(); ++i) {
        gp_Pln p;
        if(planeOfFace(b.shape.face(i), p) && p.Axis().Direction().IsEqual(n, 1e-6) && std::fabs(p.Location().Z() - z) < 1e-6)
            return areaOf(b.shape.face(i));
    }
    return 0.0;
}

struct TaperedBox {
    Document doc;
    FeatureId sketch = 0, extrude = 0;
    TaperedBox(const std::string &taper, ExtrudeDirection dir = ExtrudeDirection::OneSide, const std::string &taper2 = {}) {
        sketch = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {-10, -10}, {10, 10}));
        auto e = extrudeAll(doc, sketch, "10 mm");
        e->direction = dir;
        e->taper = doc.makeSlot(taper);
        if(dir == ExtrudeDirection::TwoSides) {
            e->distance2 = doc.makeSlot("5 mm");
            if(!taper2.empty()) e->taper2 = doc.makeSlot(taper2);
        }
        extrude = doc.addFeature(e);
    }
};

} // namespace

TEST_CASE("taper: positive flares out, negative draws in") {
    const double grow = 2 * 10 * std::tan(10 * PI / 180);
    SUBCASE("outward") {
        TaperedBox t("10 deg");
        REQUIRE(t.doc.statusOf(t.extrude).isOk());
        const Body *b = onlyBody(t.doc.displayedState());
        REQUIRE(b);
        CHECK(BRepCheck_Analyzer(b->shape.shape()).IsValid());
        CHECK(b->shape.faceCount() == 6);
        CHECK(volumeOf(b->shape.shape()) == doctest::Approx(frustum(20, 20 + grow, 10)).epsilon(1e-6));
        CHECK(faceArea(*b, gp_Dir(0, 0, 1), 10) == doctest::Approx((20 + grow) * (20 + grow)).epsilon(1e-6));
        CHECK(faceArea(*b, gp_Dir(0, 0, -1), 0) == doctest::Approx(400).epsilon(1e-9));
        // Faces keep their extrude names.
        int sides = 0;
        for(int i = 1; i <= b->shape.faceCount(); ++i) sides += b->shape.faceName(i).find("/side/") != std::string::npos;
        CHECK(sides == 4);
    }
    SUBCASE("inward") {
        TaperedBox t("-10 deg");
        REQUIRE(t.doc.statusOf(t.extrude).isOk());
        const Body *b = onlyBody(t.doc.displayedState());
        CHECK(volumeOf(b->shape.shape()) == doctest::Approx(frustum(20, 20 - grow, 10)).epsilon(1e-6));
    }
    SUBCASE("symmetric: both halves taper away from the sketch plane") {
        TaperedBox t("-5 deg", ExtrudeDirection::Symmetric);
        REQUIRE(t.doc.statusOf(t.extrude).isOk());
        const Body *b = onlyBody(t.doc.displayedState());
        REQUIRE(b);
        CHECK(BRepCheck_Analyzer(b->shape.shape()).IsValid());
        const double shrink = 2 * 10 * std::tan(5 * PI / 180);
        CHECK(volumeOf(b->shape.shape()) == doctest::Approx(2 * frustum(20, 20 - shrink, 10)).epsilon(1e-6));
    }
    SUBCASE("two sides with different tapers") {
        TaperedBox t("5 deg", ExtrudeDirection::TwoSides, "-8 deg");
        REQUIRE(t.doc.statusOf(t.extrude).isOk());
        const Body *b = onlyBody(t.doc.displayedState());
        REQUIRE(b);
        const double up = 20 + 2 * 10 * std::tan(5 * PI / 180), down = 20 - 2 * 5 * std::tan(8 * PI / 180);
        CHECK(volumeOf(b->shape.shape()) == doctest::Approx(frustum(20, up, 10) + frustum(20, down, 5)).epsilon(1e-6));
    }
    SUBCASE("editing the taper updates the solid; zero taper is a plain prism") {
        TaperedBox t("10 deg");
        auto e = std::static_pointer_cast<ExtrudeFeature>(t.doc.feature(t.extrude)->clone());
        e->taper.expr = "0 deg";
        t.doc.replaceFeature(e);
        CHECK(totalVolume(t.doc.displayedState()) == doctest::Approx(4000));
    }
    SUBCASE("a profile with a hole") {
        Document doc;
        auto s = rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {-20, -20}, {20, 20});
        s->sketch.addCircle(Vec2{0, 0}, 5);
        const FeatureId sid = doc.addFeature(s);
        auto e = std::make_shared<ExtrudeFeature>();
        const auto st = doc.stateAt(1);
        for(const auto &p : st->sketches.at(sid)->profiles)
            if(std::fabs(p.area) > 1000) e->profiles.push_back({sid, p.key, p.sample});
        REQUIRE(e->profiles.size() == 1);
        e->distance = doc.makeSlot("10");
        e->taper = doc.makeSlot("-3 deg");
        const FeatureId eid = doc.addFeature(e);
        REQUIRE(doc.statusOf(eid).isOk());
        const Body *b = onlyBody(doc.displayedState());
        REQUIRE(b);
        CHECK(BRepCheck_Analyzer(b->shape.shape()).IsValid());
        // Drawing in shrinks the outline and widens the hole.
        const double plain = 1600 * 10 - PI * 25 * 10;
        CHECK(volumeOf(b->shape.shape()) < plain - 50);
        CHECK(faceArea(*b, gp_Dir(0, 0, 1), 10) < 1600 - PI * 25 - 50);
    }
}

TEST_CASE("to object: up to a parallel face") {
    Document doc;
    // A slab from z = 30 to z = 40, and a post sketched at z = 0 extruded up to its bottom face.
    auto plane = std::make_shared<ConstructionPlaneFeature>();
    plane->base = PlaneRef::origin(PlaneRef::Kind::XY);
    plane->offset = doc.makeSlot("30 mm");
    const FeatureId pid = doc.addFeature(plane);
    const FeatureId slabSketch = doc.addFeature(rectSketch(PlaneRef::construction(pid), {-30, -30}, {30, 30}));
    doc.addFeature(extrudeAll(doc, slabSketch, "10 mm"));
    const Body *slab = onlyBody(doc.displayedState());
    REQUIRE(slab);
    const int bottom = planarFaceWithNormal(*slab, gp_Dir(0, 0, -1));
    REQUIRE(bottom > 0);
    const TopoRef bottomRef = makeTopoRef(*slab, TopoKind::Face, bottom);

    const FeatureId post = doc.addFeature(circleSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, 5));
    auto e = extrudeAll(doc, post, "1 mm");
    e->extent = ExtentType::ToObject;
    e->toObject = bottomRef;
    const FeatureId eid = doc.addFeature(e);
    REQUIRE(doc.statusOf(eid).isOk());
    const auto st = doc.displayedState();
    REQUIRE(st->bodies.size() == 2);
    const Body *postBody = st->body(bodyIdFor(eid));
    REQUIRE(postBody);
    CHECK(volumeOf(postBody->shape.shape()) == doctest::Approx(PI * 25 * 30).epsilon(1e-6));

    // It follows the target: move the slab up and the post grows.
    auto p2 = std::static_pointer_cast<ConstructionPlaneFeature>(doc.feature(pid)->clone());
    p2->offset.expr = "45 mm";
    doc.replaceFeature(p2);
    REQUIRE(doc.statusOf(eid).isOk());
    CHECK(volumeOf(doc.displayedState()->body(bodyIdFor(eid))->shape.shape()) == doctest::Approx(PI * 25 * 45).epsilon(1e-6));
}

TEST_CASE("to object: up to an inclined face") {
    Document doc;
    // A wedge whose top face is the plane z = x / 2.
    auto tri = std::make_shared<SketchFeature>();
    tri->plane = PlaneRef::origin(PlaneRef::Kind::XZ);
    const int a = tri->sketch.addPoint(0, 0), b = tri->sketch.addPoint(50, 0), c = tri->sketch.addPoint(50, 25);
    tri->sketch.addLine(a, b);
    tri->sketch.addLine(b, c);
    tri->sketch.addLine(c, a);
    const FeatureId triId = doc.addFeature(tri);
    auto wedge = extrudeAll(doc, triId, "50 mm");
    wedge->direction = ExtrudeDirection::Symmetric;
    doc.addFeature(wedge);
    const Body *w = onlyBody(doc.displayedState());
    REQUIRE(w);
    const int slanted = findFace(*w, [](const TopoDS_Face &f) {
        gp_Pln p;
        return planeOfFace(f, p) && std::fabs(p.Axis().Direction().Z()) > 0.5 && std::fabs(p.Axis().Direction().X()) > 0.3;
    });
    REQUIRE(slanted > 0);

    // A 5 mm radius post at x = 30 from z = 0 up to the slanted face: mean height 15.
    const FeatureId post = doc.addFeature(circleSketch(PlaneRef::origin(PlaneRef::Kind::XY), {30, 0}, 5));
    auto e = extrudeAll(doc, post, "1 mm");
    e->extent = ExtentType::ToObject;
    e->toObject = makeTopoRef(*w, TopoKind::Face, slanted);
    const FeatureId eid = doc.addFeature(e);
    REQUIRE(doc.statusOf(eid).isOk());
    const Body *pb = doc.displayedState()->body(bodyIdFor(eid));
    REQUIRE(pb);
    CHECK(BRepCheck_Analyzer(pb->shape.shape()).IsValid());
    CHECK(volumeOf(pb->shape.shape()) == doctest::Approx(PI * 25 * 15).epsilon(1e-6));
    // The trimmed top carries the extrude's end-cap name.
    bool named = false;
    for(int i = 1; i <= pb->shape.faceCount(); ++i) named |= pb->shape.faceName(i).find("/end/") != std::string::npos;
    CHECK(named);

    // A face that is not reached is an error, not a crash.
    auto miss = extrudeAll(doc, post, "1 mm");
    miss->extent = ExtentType::ToObject;
    miss->toObject = TopoRef();
    const FeatureId mid = doc.addFeature(miss);
    CHECK(doc.statusOf(mid).isError());
}
