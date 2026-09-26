// Solid features built through the Document, with volumes checked against
// closed-form values, and topological naming across upstream edits.
#include <doctest.h>

#include "TestModels.h"

#include <BRepAdaptor_Curve.hxx>

using namespace cadtest;

namespace {

constexpr double PI = 3.14159265358979323846;

struct Box {
    Document doc;
    FeatureId sketch = 0, extrude = 0;
    Box(double w = 40, double d = 20, double h = 10) {
        sketch = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {w, d}));
        extrude = doc.addFeature(extrudeAll(doc, sketch, std::to_string(h) + " mm"));
    }
    const Body &body() {
        auto s = doc.displayedState();
        REQUIRE(s->bodies.size() == 1);
        return *s->bodies.begin()->second;
    }
    TopoRef topFace() {
        const Body &b = body();
        const int i = planarFaceWithNormal(b, gp_Dir(0, 0, 1));
        REQUIRE(i > 0);
        return makeTopoRef(b, TopoKind::Face, i);
    }
    Bnd_Box bbox() { return boundingBox(body().shape.shape()); }
};

void checkOk(Document &doc) {
    for(const auto &f : doc.features()) {
        const Status s = doc.statusOf(f->id);
        CHECK_MESSAGE(s.isOk(), f->name << ": " << s.message);
    }
}

} // namespace

TEST_CASE("extrude a rectangle into a named box") {
    Box box;
    checkOk(box.doc);
    const Body &b = box.body();
    CHECK(b.id == "b2");
    CHECK(volumeOf(b.shape.shape()) == doctest::Approx(8000));
    CHECK(b.shape.faceCount() == 6);
    CHECK(b.shape.edgeCount() == 12);
    const int top = b.shape.indexOfName(TopoKind::Face, "f2/end/s1:c5.0");
    REQUIRE(top > 0);
    gp_Pln pln;
    REQUIRE(planeOfFace(b.shape.face(top), pln));
    CHECK(pln.Axis().Direction().IsEqual(gp_Dir(0, 0, 1), 1e-9));
    CHECK(pln.Location().Z() == doctest::Approx(10));
    CHECK(b.shape.indexOfName(TopoKind::Face, "f2/start/s1:c5.0") > 0);
    CHECK(b.shape.indexOfName(TopoKind::Face, "f2/side/s1:c6.0") > 0);
    // Edge between the right side wall and the top cap.
    CHECK(b.shape.indexOfName(TopoKind::Edge, "{f2/end/s1:c5.0|f2/side/s1:c6.0}") > 0);
}

TEST_CASE("extrude directions: symmetric, two sides, flipped") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {40, 20}));
    auto e = extrudeAll(doc, s, "5 mm");
    e->direction = ExtrudeDirection::Symmetric;
    const FeatureId eid = doc.addFeature(e);
    Bnd_Box bb = boundingBox(doc.displayedState()->bodies.begin()->second->shape.shape());
    double x0, y0, z0, x1, y1, z1;
    bb.Get(x0, y0, z0, x1, y1, z1);
    CHECK(z0 == doctest::Approx(-5).epsilon(1e-6));
    CHECK(z1 == doctest::Approx(5).epsilon(1e-6));

    auto two = std::static_pointer_cast<ExtrudeFeature>(doc.feature(eid)->clone());
    two->direction = ExtrudeDirection::TwoSides;
    two->distance.expr = "10";
    two->distance2 = doc.makeSlot("4");
    doc.replaceFeature(two);
    bb = boundingBox(doc.displayedState()->bodies.begin()->second->shape.shape());
    bb.Get(x0, y0, z0, x1, y1, z1);
    CHECK(z0 == doctest::Approx(-4).epsilon(1e-6));
    CHECK(z1 == doctest::Approx(10).epsilon(1e-6));

    auto flipped = std::static_pointer_cast<ExtrudeFeature>(doc.feature(eid)->clone());
    flipped->direction = ExtrudeDirection::OneSide;
    flipped->flip = true;
    doc.replaceFeature(flipped);
    bb = boundingBox(doc.displayedState()->bodies.begin()->second->shape.shape());
    bb.Get(x0, y0, z0, x1, y1, z1);
    CHECK(z0 == doctest::Approx(-10).epsilon(1e-6));
    CHECK(z1 == doctest::Approx(0).epsilon(1e-6));
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(8000));
}

TEST_CASE("cut a hole through all") {
    Box box;
    const FeatureId c = box.doc.addFeature(circleSketch(PlaneRef::origin(PlaneRef::Kind::XY), {20, 10}, 5));
    auto cut = extrudeAll(box.doc, c, "1 mm", BodyOperation::Cut);
    cut->extent = ExtentType::ThroughAll;
    cut->direction = ExtrudeDirection::Symmetric;
    box.doc.addFeature(cut);
    checkOk(box.doc);
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(8000 - PI * 25 * 10).epsilon(1e-6));
    // The hole wall is one cylindrical face named after the circle.
    CHECK(box.body().shape.indexOfName(TopoKind::Face, "f4/side/s3:c2.0") > 0);
}

TEST_CASE("sketch on a face follows an upstream height change") {
    Box box;
    const TopoRef top = box.topFace();
    CHECK(top.name == "f2/end/s1:c5.0");
    auto boss = std::make_shared<SketchFeature>();
    boss->plane = PlaneRef::onFace(top);
    boss->sketch.addCircle(Vec2{20, 10}, 5);
    const FeatureId bs = box.doc.addFeature(boss);
    box.doc.addFeature(extrudeAll(box.doc, bs, "5 mm", BodyOperation::Join));
    checkOk(box.doc);
    CHECK(box.doc.displayedState()->bodies.size() == 1);
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(8000 + PI * 25 * 5).epsilon(1e-6));

    // Make the base taller: the boss sketch must move up with the top face.
    auto ex = std::static_pointer_cast<ExtrudeFeature>(box.doc.feature(box.extrude)->clone());
    ex->distance.expr = "20 mm";
    box.doc.replaceFeature(ex);
    checkOk(box.doc);
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(16000 + PI * 25 * 5).epsilon(1e-6));
    double x0, y0, z0, x1, y1, z1;
    box.bbox().Get(x0, y0, z0, x1, y1, z1);
    CHECK(z1 == doctest::Approx(25).epsilon(1e-6));
}

TEST_CASE("fillet survives a change of the sketch width") {
    Box box;
    const Body &b = box.body();
    const int e = edgeNear(b, gp_Pnt(40, 0, 5));
    REQUIRE(e > 0);
    auto fil = std::make_shared<FilletFeature>();
    fil->edges.push_back(makeTopoRef(b, TopoKind::Edge, e));
    fil->radius = box.doc.makeSlot("2 mm");
    box.doc.addFeature(fil);
    checkOk(box.doc);
    const double cornerLoss = (4 - PI) * 10; // (r^2 - pi r^2 / 4) * h
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(8000 - cornerLoss).epsilon(1e-6));
    // The fillet face is named after the edge it replaced.
    CHECK(box.body().shape.indexOfName(TopoKind::Face, "f3/fillet/{f2/side/s1:c5.0|f2/side/s1:c6.0}") > 0);

    // Widen the rectangle from 40 to 60 by moving its right-hand points.
    auto sk = std::static_pointer_cast<SketchFeature>(box.doc.feature(box.sketch)->clone());
    for(auto &ent : sk->sketch.entities)
        if(ent.type == SkType::Point && ent.x > 39) ent.x = 60;
    box.doc.replaceFeature(sk);
    checkOk(box.doc);
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(12000 - cornerLoss).epsilon(1e-6));
}

TEST_CASE("fillet and chamfer on several edges; errors do not crash") {
    Box box;
    const Body &b = box.body();
    auto ch = std::make_shared<ChamferFeature>();
    ch->edges.push_back(makeTopoRef(b, TopoKind::Edge, edgeNear(b, gp_Pnt(20, 0, 10))));
    ch->edges.push_back(makeTopoRef(b, TopoKind::Edge, edgeNear(b, gp_Pnt(20, 20, 10))));
    ch->distance = box.doc.makeSlot("1 mm");
    const FeatureId cid = box.doc.addFeature(ch);
    checkOk(box.doc);
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(8000 - 2 * 0.5 * 1 * 1 * 40).epsilon(1e-6));

    // A fillet far too large must report an error and leave the model intact.
    auto fil = std::make_shared<FilletFeature>();
    const Body &b2 = box.body();
    fil->edges.push_back(makeTopoRef(b2, TopoKind::Edge, edgeNear(b2, gp_Pnt(0, 10, 0))));
    fil->radius = box.doc.makeSlot("50 mm");
    const FeatureId fid = box.doc.addFeature(fil);
    CHECK(box.doc.statusOf(fid).isError());
    CHECK(box.doc.statusOf(cid).isOk());
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(8000 - 40).epsilon(1e-6));
}

TEST_CASE("a cut that splits a body keeps ids; split faces get suffixes") {
    Box box;
    const TopoRef oldTop = box.topFace();
    const FeatureId slot = box.doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {10, -5}, {14, 25}));
    auto cut = extrudeAll(box.doc, slot, "1", BodyOperation::Cut);
    cut->extent = ExtentType::ThroughAll;
    cut->direction = ExtrudeDirection::Symmetric;
    box.doc.addFeature(cut);
    checkOk(box.doc);
    auto s = box.doc.displayedState();
    REQUIRE(s->bodies.size() == 2);
    CHECK(volumeOf(s->bodies.at("b2")->shape.shape()) == doctest::Approx(5200));
    CHECK(volumeOf(s->bodies.at("b2.2")->shape.shape()) == doctest::Approx(2000));
    CHECK(s->bodies.at("b2")->name == "Body1");

    // The old top face no longer exists as such; it resolves (with a warning) to a piece.
    ResolvedRef r = resolveRef(*s, oldTop);
    CHECK(r.ok);
    CHECK(r.status.severity == Severity::Warning);
    const std::string n = r.body->shape.faceName(r.index);
    CHECK(baseName(n) == "f2/end/s1:c5.0");
    CHECK(n != "f2/end/s1:c5.0");
}

TEST_CASE("groove splits the top face inside one body") {
    Box box;
    auto groove = std::make_shared<SketchFeature>();
    groove->plane = PlaneRef::onFace(box.topFace());
    groove->sketch.addRectangle({18, -5}, {22, 25});
    const FeatureId g = box.doc.addFeature(groove);
    auto cut = extrudeAll(box.doc, g, "-5 mm", BodyOperation::Cut);
    box.doc.addFeature(cut);
    checkOk(box.doc);
    const Body &b = box.body();
    CHECK(volumeOf(b.shape.shape()) == doctest::Approx(8000 - 4 * 20 * 5));
    CHECK(b.shape.indexOfName(TopoKind::Face, "f2/end/s1:c5.0~1") > 0);
    CHECK(b.shape.indexOfName(TopoKind::Face, "f2/end/s1:c5.0~2") > 0);
}

TEST_CASE("holes: simple, drill point, counterbore, countersink") {
    struct Case {
        HoleType type;
        bool throughAll;
        bool flat;
        double removed;
    };
    const double r = 2.5;
    const double tipH = r / std::tan(59.0 * PI / 180.0);
    const double frustum = PI * 2.5 / 3 * (25 + 12.5 + 6.25);
    const Case cases[] = {
        {HoleType::Simple, false, true, PI * r * r * 6},
        {HoleType::Simple, false, false, PI * r * r * 6 + PI * r * r * tipH / 3},
        {HoleType::Simple, true, true, PI * r * r * 10},
        {HoleType::Counterbore, true, true, PI * 25 * 3 + PI * r * r * 7},
        {HoleType::Countersink, true, true, frustum + PI * r * r * 7.5},
    };
    for(const auto &c : cases) {
        Box box;
        auto h = std::make_shared<HoleFeature>();
        h->face = box.topFace();
        h->points = {{20, 10}};
        h->holeType = c.type;
        h->extent = c.throughAll ? ExtentType::ThroughAll : ExtentType::Distance;
        h->flatTip = c.flat;
        h->diameter = box.doc.makeSlot("5 mm");
        h->depth = box.doc.makeSlot("6 mm");
        h->tipAngle = box.doc.makeSlot("118 deg");
        h->cboreDiameter = box.doc.makeSlot("10 mm");
        h->cboreDepth = box.doc.makeSlot("3 mm");
        h->csinkDiameter = box.doc.makeSlot("10 mm");
        h->csinkAngle = box.doc.makeSlot("90 deg");
        box.doc.addFeature(h);
        checkOk(box.doc);
        CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(8000 - c.removed).epsilon(1e-5));
    }
}

TEST_CASE("combine join / cut with and without keeping tools") {
    auto build = [](Document &doc) {
        const FeatureId a = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {40, 20}));
        doc.addFeature(extrudeAll(doc, a, "10"));
        const FeatureId b = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {30, 5}, {50, 15}));
        doc.addFeature(extrudeAll(doc, b, "10"));
        REQUIRE(doc.displayedState()->bodies.size() == 2);
    };
    {
        Document doc;
        build(doc);
        auto c = std::make_shared<CombineFeature>();
        c->target = "b2";
        c->tools = {"b4"};
        c->operation = BodyOperation::Join;
        doc.addFeature(c);
        checkOk(doc);
        auto s = doc.displayedState();
        REQUIRE(s->bodies.size() == 1);
        CHECK(totalVolume(s) == doctest::Approx(9000));
        CHECK(s->resolveBodyId("b4") == "b2");
    }
    {
        Document doc;
        build(doc);
        auto c = std::make_shared<CombineFeature>();
        c->target = "b2";
        c->tools = {"b4"};
        c->operation = BodyOperation::Cut;
        c->keepTools = true;
        doc.addFeature(c);
        checkOk(doc);
        auto s = doc.displayedState();
        REQUIRE(s->bodies.size() == 2);
        CHECK(volumeOf(s->bodies.at("b2")->shape.shape()) == doctest::Approx(7000));
        CHECK(volumeOf(s->bodies.at("b4")->shape.shape()) == doctest::Approx(2000));
    }
    {
        Document doc;
        build(doc);
        auto c = std::make_shared<CombineFeature>();
        c->target = "b2";
        c->tools = {"b4"};
        c->operation = BodyOperation::Intersect;
        doc.addFeature(c);
        checkOk(doc);
        auto s = doc.displayedState();
        REQUIRE(s->bodies.size() == 1);
        CHECK(totalVolume(s) == doctest::Approx(1000));
    }
}

TEST_CASE("join an overlapping extrude into the existing body automatically") {
    Box box;
    const FeatureId s2 = box.doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {30, 5}, {50, 15}));
    box.doc.addFeature(extrudeAll(box.doc, s2, "10", BodyOperation::Join));
    checkOk(box.doc);
    CHECK(box.doc.displayedState()->bodies.size() == 1);
    CHECK(box.body().id == "b2");
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(9000));
}

TEST_CASE("construction planes: offset and rotated") {
    Document doc;
    auto p = std::make_shared<ConstructionPlaneFeature>();
    p->base = PlaneRef::origin(PlaneRef::Kind::XY);
    p->offset = doc.makeSlot("15 mm");
    const FeatureId pid = doc.addFeature(p);
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::construction(pid), {0, 0}, {10, 10}));
    doc.addFeature(extrudeAll(doc, s, "5"));
    checkOk(doc);
    double x0, y0, z0, x1, y1, z1;
    boundingBox(doc.displayedState()->bodies.begin()->second->shape.shape()).Get(x0, y0, z0, x1, y1, z1);
    CHECK(z0 == doctest::Approx(15).epsilon(1e-6));
    CHECK(z1 == doctest::Approx(20).epsilon(1e-6));

    // Rotate the plane 90 degrees about its local X axis: its normal becomes -Y.
    auto rot = std::static_pointer_cast<ConstructionPlaneFeature>(doc.feature(pid)->clone());
    rot->offset.expr = "0";
    rot->angle = doc.makeSlot("90 deg");
    rot->axis = PlaneRotationAxis::LocalX;
    doc.replaceFeature(rot);
    checkOk(doc);
    const gp_Ax3 frame = doc.displayedState()->planes.at(pid)->frame;
    CHECK(frame.Direction().IsEqual(gp_Dir(0, -1, 0), 1e-9));
    boundingBox(doc.displayedState()->bodies.begin()->second->shape.shape()).Get(x0, y0, z0, x1, y1, z1);
    CHECK(y0 == doctest::Approx(-5).epsilon(1e-6));
    CHECK(y1 == doctest::Approx(0).epsilon(1e-6));
    CHECK(z1 == doctest::Approx(10).epsilon(1e-6));

    // Any angle works, e.g. 30 degrees.
    auto rot30 = std::static_pointer_cast<ConstructionPlaneFeature>(doc.feature(pid)->clone());
    rot30->angle.expr = "30";
    doc.replaceFeature(rot30);
    checkOk(doc);
    const gp_Dir n = doc.displayedState()->planes.at(pid)->frame.Direction();
    CHECK(n.Z() == doctest::Approx(std::cos(PI / 6)));
    CHECK(n.Y() == doctest::Approx(-std::sin(PI / 6)));
}

TEST_CASE("extrude a planar face of a body (press-pull style)") {
    Box box;
    auto e = std::make_shared<ExtrudeFeature>();
    e->faces.push_back(box.topFace());
    e->distance = box.doc.makeSlot("5 mm");
    e->operation = BodyOperation::Join;
    box.doc.addFeature(e);
    checkOk(box.doc);
    CHECK(volumeOf(box.body().shape.shape()) == doctest::Approx(12000));
}

TEST_CASE("feature errors: lost profile, zero distance, nothing to cut") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {10, 10}));
    auto e = extrudeAll(doc, s, "0 mm");
    const FeatureId eid = doc.addFeature(e);
    CHECK(doc.statusOf(eid).isError());
    auto cut = extrudeAll(doc, s, "5", BodyOperation::Cut);
    const FeatureId cid = doc.addFeature(cut);
    CHECK(doc.statusOf(cid).isError());
    auto bad = std::make_shared<ExtrudeFeature>();
    bad->profiles.push_back({s, "c99.0,c98.0", {500, 500}});
    bad->distance = doc.makeSlot("5");
    const FeatureId bid = doc.addFeature(bad);
    CHECK(doc.statusOf(bid).isError());
    CHECK(doc.displayedState()->bodies.empty());
}
