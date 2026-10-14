// Align: the transform for each pair of picks, and the timeline feature.
#include <doctest.h>

#include "TestModels.h"
#include "features/AlignFeature.h"
#include "geom/Align.h"
#include "sketch/SketchOps.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>

using namespace cadtest;

namespace {

gp_Pnt apply(const gp_Trsf &t, gp_Pnt p) { return p.Transformed(t); }
gp_Dir apply(const gp_Trsf &t, gp_Dir d) { return d.Transformed(t); }

AlignAnchor point(double x, double y, double z) { return {AlignKind::Point, gp_Pnt(x, y, z), gp_Dir(0, 0, 1), true}; }
AlignAnchor line(gp_Pnt p, gp_Dir d, bool bounded = true) { return {AlignKind::Line, p, d, bounded}; }
AlignAnchor plane(gp_Pnt p, gp_Dir n) { return {AlignKind::Plane, p, n, true}; }

// Two separate boxes: A 10 x 10 x 10 at the origin, B 20 x 20 x 5 off to the side.
struct TwoBoxes {
    Document doc;
    BodyId a, b;
    TwoBoxes() {
        const FeatureId sa = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {10, 10}));
        doc.addFeature(extrudeAll(doc, sa, "10"));
        a = doc.displayedState()->bodies.begin()->first;
        const FeatureId sb = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {30, 0}, {50, 20}));
        doc.addFeature(extrudeAll(doc, sb, "5"));
        for(const auto &kv : doc.displayedState()->bodies)
            if(kv.first != a) b = kv.first;
        REQUIRE(!b.empty());
    }
    const Body &body(const BodyId &id) { return *doc.displayedState()->body(id); }
    AlignRef face(const BodyId &id, gp_Dir n) {
        AlignRef r;
        r.ref = makeTopoRef(body(id), TopoKind::Face, planarFaceWithNormal(body(id), n));
        return r;
    }
    Bnd_Box box(const BodyId &id) {
        Bnd_Box bb;
        BRepBndLib::AddOptimal(body(id).shape.shape(), bb, false, false);
        return bb;
    }
};

void checkBox(Bnd_Box b, double x0, double y0, double z0, double x1, double y1, double z1) {
    double a0, b0, c0, a1, b1, c1;
    b.Get(a0, b0, c0, a1, b1, c1);
    CHECK(a0 == doctest::Approx(x0).epsilon(1e-6));
    CHECK(b0 == doctest::Approx(y0).epsilon(1e-6));
    CHECK(c0 == doctest::Approx(z0).epsilon(1e-6));
    CHECK(a1 == doctest::Approx(x1).epsilon(1e-6));
    CHECK(b1 == doctest::Approx(y1).epsilon(1e-6));
    CHECK(c1 == doctest::Approx(z1).epsilon(1e-6));
}

} // namespace

TEST_CASE("align: what each pair of picks does") {
    std::string why;
    // Point to point: a plain move.
    auto t = alignTransform(point(1, 2, 3), point(4, 6, 3), false, 0, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Pnt(1, 2, 3)).Distance(gp_Pnt(4, 6, 3)) < 1e-9);
    CHECK(apply(*t, gp_Pnt(0, 0, 0)).Distance(gp_Pnt(3, 4, 0)) < 1e-9);
    // Point to plane: straight across, onto it.
    t = alignTransform(point(1, 2, 3), plane(gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1)), false, 0, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Pnt(1, 2, 3)).Distance(gp_Pnt(1, 2, 10)) < 1e-9);
    // Point to an endless line: the nearest point on it.
    t = alignTransform(point(3, 4, 0), line(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), false), false, 0, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Pnt(3, 4, 0)).Distance(gp_Pnt(3, 0, 0)) < 1e-9);
    // Line to line: along it, end on end; flipped the other way.
    const AlignAnchor from = line(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), to = line(gp_Pnt(5, 5, 5), gp_Dir(0, 0, 1));
    t = alignTransform(from, to, false, 0, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Pnt(0, 0, 0)).Distance(gp_Pnt(5, 5, 5)) < 1e-9);
    CHECK(apply(*t, gp_Dir(1, 0, 0)).IsEqual(gp_Dir(0, 0, 1), 1e-9));
    t = alignTransform(from, to, true, 0, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Dir(1, 0, 0)).IsEqual(gp_Dir(0, 0, -1), 1e-9));
    // Plane to plane: flush (facing each other), centre on centre; flipped: the same way.
    const AlignAnchor bottom = plane(gp_Pnt(5, 5, 0), gp_Dir(0, 0, -1)), top = plane(gp_Pnt(40, 10, 5), gp_Dir(0, 0, 1));
    t = alignTransform(bottom, top, false, 0, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Pnt(5, 5, 0)).Distance(gp_Pnt(40, 10, 5)) < 1e-9);
    CHECK(apply(*t, gp_Dir(0, 0, -1)).IsEqual(gp_Dir(0, 0, -1), 1e-9));
    t = alignTransform(bottom, top, true, 0, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Dir(0, 0, -1)).IsEqual(gp_Dir(0, 0, 1), 1e-9));
    // Angle: turned about the shared normal, through the target's centre.
    t = alignTransform(bottom, top, false, M_PI / 2, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Pnt(5, 5, 0)).Distance(gp_Pnt(40, 10, 5)) < 1e-9);
    CHECK(apply(*t, gp_Dir(1, 0, 0)).IsEqual(gp_Dir(0, 1, 0), 1e-9));
    // A line laid in a plane.
    t = alignTransform(line(gp_Pnt(0, 0, 3), gp_Dir(1, 0, 1)), plane(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), false, 0, why);
    REQUIRE(t);
    CHECK(apply(*t, gp_Pnt(0, 0, 3)).Z() == doctest::Approx(0).epsilon(1e-9));
    CHECK(std::fabs(apply(*t, gp_Dir(1, 0, 1)).Z()) < 1e-9);
    // A face onto a line has no single answer.
    CHECK(!alignTransform(bottom, to, false, 0, why));
    CHECK(!why.empty());
    // Sketches: a half turn stays in their plane.
    t = alignTransform(line(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), line(gp_Pnt(0, 0, 0), gp_Dir(-1, 0, 0)), false, 0, why,
                       gp_Dir(0, 0, 1));
    REQUIRE(t);
    CHECK(apply(*t, gp_Dir(0, 1, 0)).IsEqual(gp_Dir(0, -1, 0), 1e-9));
    for(AlignKind f : {AlignKind::Point, AlignKind::Line, AlignKind::Plane})
        for(AlignKind g : {AlignKind::Point, AlignKind::Line, AlignKind::Plane}) CHECK(!describeAlign(f, g).empty());
}

TEST_CASE("align a box face to face onto another, and things built on it after") {
    TwoBoxes m;
    const double before = totalVolume(m.doc.displayedState());
    // Before the move: a reference to the box's top face.
    const TopoRef topFace = makeTopoRef(m.body(m.a), TopoKind::Face, planarFaceWithNormal(m.body(m.a), gp_Dir(0, 0, 1)));
    auto al = std::make_shared<AlignFeature>();
    al->bodies = {m.a};
    al->from = m.face(m.a, gp_Dir(0, 0, -1));
    al->to = m.face(m.b, gp_Dir(0, 0, 1));
    m.doc.addFeature(al);
    INFO(m.doc.statusOf(al->id).message);
    REQUIRE(m.doc.statusOf(al->id).isOk());
    // A sits on B, centre on centre.
    checkBox(m.box(m.a), 35, 5, 5, 45, 15, 15);
    checkBox(m.box(m.b), 30, 0, 0, 50, 20, 5);
    CHECK(totalVolume(m.doc.displayedState()) == doctest::Approx(before).epsilon(1e-9));
    // The face found from before the move is still found on the moved box.
    const ResolvedRef r = resolveRef(*m.doc.displayedState(), topFace);
    REQUIRE(r.ok);
    CHECK(!r.status.isError());
    gp_Pln pl;
    REQUIRE(planeOfFace(TopoDS::Face(r.shape), pl));
    CHECK(pl.Location().Z() == doctest::Approx(15).epsilon(1e-9));

    // Flipped and turned a quarter: stacked facing the same way (the box hangs below the face).
    auto edited = std::static_pointer_cast<AlignFeature>(al->clone());
    edited->flip = true;
    edited->angle = m.doc.makeSlot("90 deg");
    m.doc.replaceFeature(edited);
    REQUIRE(m.doc.statusOf(al->id).isOk());
    checkBox(m.box(m.a), 35, 5, -5, 45, 15, 5);

    // Saved and read back.
    const json j = edited->dataToJson();
    AlignFeature back;
    back.dataFromJson(j);
    CHECK(back.bodies == edited->bodies);
    CHECK(back.flip);
    CHECK(back.angle.expr == edited->angle.expr);
    CHECK(back.to.ref.toJson() == edited->to.ref.toJson());
}

TEST_CASE("align a copy, edge to edge, and what align refuses") {
    TwoBoxes m;
    const double before = totalVolume(m.doc.displayedState());
    // A's edge along X at the bottom front, clicked near its start, onto B's
    // vertical edge at (30, 0), clicked near its bottom.
    auto edge = [&](const BodyId &id, gp_Pnt mid, gp_Pnt click) {
        AlignRef r;
        r.ref = makeTopoRef(m.body(id), TopoKind::Edge, edgeNear(m.body(id), mid));
        r.at = click;
        return r;
    };
    auto al = std::make_shared<AlignFeature>();
    al->bodies = {m.a};
    al->from = edge(m.a, gp_Pnt(5, 0, 0), gp_Pnt(0.5, 0, 0));
    al->to = edge(m.b, gp_Pnt(30, 0, 2.5), gp_Pnt(30, 0, 0.2));
    al->copy = true;
    m.doc.addFeature(al);
    INFO(m.doc.statusOf(al->id).message);
    REQUIRE(m.doc.statusOf(al->id).isOk());
    // The original stays; the copy stands its edge up along B's, from B's bottom corner.
    CHECK(m.doc.displayedState()->bodies.size() == 3);
    CHECK(totalVolume(m.doc.displayedState()) == doctest::Approx(before + 1000).epsilon(1e-9));
    checkBox(m.box(m.a), 0, 0, 0, 10, 10, 10);

    // From must be on the bodies moved, and to off them.
    auto bad = std::make_shared<AlignFeature>();
    bad->bodies = {m.a};
    bad->from = m.face(m.b, gp_Dir(0, 0, 1));
    bad->to = m.face(m.b, gp_Dir(0, 0, -1));
    m.doc.addFeature(bad);
    CHECK(m.doc.statusOf(bad->id).isError());
    // Onto an origin plane: A's top face down onto XY (it turns over and sits on it).
    auto origin = std::make_shared<AlignFeature>();
    origin->bodies = {m.a};
    origin->from = m.face(m.a, gp_Dir(0, 0, 1));
    origin->to.builtin = "xy";
    m.doc.deleteFeature(bad->id);
    m.doc.addFeature(origin);
    INFO(m.doc.statusOf(origin->id).message);
    REQUIRE(m.doc.statusOf(origin->id).isOk());
    checkBox(m.box(m.a), -5, -5, 0, 5, 5, 10);
}

TEST_CASE("align in a sketch: a corner onto a point, an edge along an axis") {
    Sketch s;
    const int a = s.addPoint(1, 1), b = s.addPoint(4, 1), c = s.addPoint(4, 3);
    const int ab = s.addLine(a, b), bc = s.addLine(b, c);
    s.addConstraint(SkCon::Horizontal, ab);
    s.addConstraint(SkCon::Vertical, bc);
    const int target = s.addPoint(10, 10);
    std::string why;
    // The corner a onto the point: a plain move.
    auto m = sketchAlignMove(s, {a, {}}, {target, {}}, false, 0, why);
    REQUIRE(m);
    CHECK(m->from == AlignKind::Point);
    Sketch moved = s;
    transformEntities(moved, {ab, bc}, m->pivot, m->delta, m->angle);
    CHECK(moved.pointPos(a).x == doctest::Approx(10));
    CHECK(moved.pointPos(c).x == doctest::Approx(13));
    CHECK(moved.pointPos(c).y == doctest::Approx(12));
    // The edge b-c (clicked near c) along the Y axis: c goes onto it, the
    // edge runs along +Y from there; flipped, along -Y.
    m = sketchAlignMove(s, {bc, Vec2(4, 2.9)}, {kSketchYAxis, {}}, false, 0, why);
    REQUIRE(m);
    CHECK(m->to == AlignKind::Line);
    moved = s;
    transformEntities(moved, {ab, bc}, m->pivot, m->delta, m->angle);
    CHECK(moved.pointPos(c).x == doctest::Approx(0).epsilon(1e-9));
    CHECK(moved.pointPos(b).x == doctest::Approx(0).epsilon(1e-9));
    CHECK(moved.pointPos(b).y > moved.pointPos(c).y);
    m = sketchAlignMove(s, {bc, Vec2(4, 2.9)}, {kSketchYAxis, {}}, true, 0, why);
    REQUIRE(m);
    moved = s;
    transformEntities(moved, {ab, bc}, m->pivot, m->delta, m->angle);
    CHECK(moved.pointPos(b).y < moved.pointPos(c).y);
    // Turned a half turn here: H and V stay (and stay right).
    int h = 0, v = 0;
    for(const SkConstraint &k : moved.constraints) {
        h += k.type == SkCon::Horizontal;
        v += k.type == SkCon::Vertical;
    }
    CHECK(h + v == 2);
}
