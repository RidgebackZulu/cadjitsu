// Every modelling feature through its whole life in a timeline: created,
// edited, suppressed and unsuppressed (from the cache), and recomputed after
// an upstream edit (the box it works on gets taller), with volumes checked
// against closed-form values.
#include <doctest.h>

#include "TestModels.h"

#include <functional>

using namespace cadtest;

namespace {

constexpr double PI = 3.14159265358979323846;

// A 60 x 40 x 20 box.
struct Plate {
    Document doc;
    FeatureId sketch = 0, extrude = 0;
    Plate() {
        sketch = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {60, 40}));
        extrude = doc.addFeature(extrudeAll(doc, sketch, "20 mm"));
    }
    const Body &body() {
        auto s = doc.displayedState();
        REQUIRE(s->bodies.size() >= 1);
        return *s->orderedBodies().front();
    }
    TopoRef topFace() {
        const Body &b = body();
        const int i = planarFaceWithNormal(b, gp_Dir(0, 0, 1));
        REQUIRE(i > 0);
        return makeTopoRef(b, TopoKind::Face, i);
    }
    TopoRef edgeAt(gp_Pnt p) {
        const Body &b = body();
        const int e = edgeNear(b, p);
        REQUIRE(e > 0);
        return makeTopoRef(b, TopoKind::Edge, e);
    }
    double volume() { return totalVolume(doc.displayedState()); }
    size_t bodies() { return doc.displayedState()->bodies.size(); }
    void setHeight(const std::string &h) { setDistance(extrude, h); }
    void setDistance(FeatureId e, const std::string &d) {
        auto x = std::static_pointer_cast<ExtrudeFeature>(doc.feature(e)->clone());
        x->distance.expr = d;
        doc.replaceFeature(x);
    }
    template <class F> void edit(FeatureId id, const std::function<void(F &)> &fn) {
        auto f = std::static_pointer_cast<F>(doc.feature(id)->clone());
        fn(*f);
        doc.replaceFeature(f);
    }
    void checkOk() {
        for(const auto &f : doc.features()) {
            const Status s = doc.statusOf(f->id);
            CHECK_MESSAGE(s.isOk(), f->name << ": " << s.message);
        }
    }
    // Suppressing `id` gives `without`; unsuppressing restores `with` from the cache.
    void suppressCycle(FeatureId id, double without, double with) {
        doc.setSuppressed(id, true);
        CHECK(volume() == doctest::Approx(without).epsilon(1e-9));
        const size_t computed = doc.computeCount();
        doc.setSuppressed(id, false);
        CHECK(volume() == doctest::Approx(with).epsilon(1e-9));
        CHECK(doc.computeCount() == computed);
    }
};

} // namespace

TEST_CASE("fillet: create, edit, suppress, upstream edit") {
    Plate p;
    auto f = std::make_shared<FilletFeature>();
    f->edges.push_back(p.edgeAt(gp_Pnt(30, 0, 20)));
    f->radius = p.doc.makeSlot("2 mm");
    const FeatureId id = p.doc.addFeature(f);
    p.checkOk();
    auto loss = [](double r, double len) { return r * r * (1 - PI / 4) * len; };
    CHECK(p.volume() == doctest::Approx(48000 - loss(2, 60)).epsilon(1e-9));
    p.edit<FilletFeature>(id, [](FilletFeature &x) { x.radius.expr = "3 mm"; });
    CHECK(p.volume() == doctest::Approx(48000 - loss(3, 60)).epsilon(1e-9));
    p.suppressCycle(id, 48000, 48000 - loss(3, 60));
    p.setHeight("30 mm");
    p.checkOk();
    CHECK(p.volume() == doctest::Approx(72000 - loss(3, 60)).epsilon(1e-9));
}

TEST_CASE("chamfer: create, edit type and distances, suppress, upstream edit") {
    Plate p;
    auto f = std::make_shared<ChamferFeature>();
    f->edges.push_back(p.edgeAt(gp_Pnt(30, 0, 20)));
    f->distance = p.doc.makeSlot("2 mm");
    const FeatureId id = p.doc.addFeature(f);
    p.checkOk();
    CHECK(p.volume() == doctest::Approx(48000 - 0.5 * 2 * 2 * 60).epsilon(1e-9));
    p.edit<ChamferFeature>(id, [&](ChamferFeature &x) {
        x.chamferType = ChamferType::TwoDistances;
        x.distance2 = p.doc.makeSlot("3 mm");
    });
    CHECK(p.volume() == doctest::Approx(48000 - 0.5 * 2 * 3 * 60).epsilon(1e-9));
    p.suppressCycle(id, 48000, 48000 - 0.5 * 2 * 3 * 60);
    p.setHeight("30 mm");
    p.checkOk();
    CHECK(p.volume() == doctest::Approx(72000 - 0.5 * 2 * 3 * 60).epsilon(1e-9));
}

TEST_CASE("hole on a face: create, edit, suppress, upstream edit") {
    Plate p;
    auto h = std::make_shared<HoleFeature>();
    h->face = p.topFace();
    h->points = {Vec2{30, 20}};
    h->extent = ExtentType::ThroughAll;
    h->diameter = p.doc.makeSlot("6 mm");
    const FeatureId id = p.doc.addFeature(h);
    p.checkOk();
    CHECK(p.volume() == doctest::Approx(48000 - PI * 9 * 20).epsilon(1e-9));
    // The drill is available to show while it is previewed.
    TimelineEvaluation eval;
    evaluateTimeline(p.doc.features(), *buildParamTable(p.doc.features()), 3, eval, *p.doc.sharedCache());
    REQUIRE(eval.tools.size() == 3);
    REQUIRE(eval.tools[2]);
    // Through all ends just past the plate.
    const Bnd_Box tb = boundingBox(eval.tools[2]->shape.shape());
    CHECK(tb.CornerMin().Z() > -3.0);
    CHECK(tb.CornerMin().Z() < 0.0);
    p.edit<HoleFeature>(id, [](HoleFeature &x) {
        x.diameter.expr = "8 mm";
        x.points.push_back(Vec2{10, 10});
    });
    CHECK(p.volume() == doctest::Approx(48000 - 2 * PI * 16 * 20).epsilon(1e-9));
    p.suppressCycle(id, 48000, 48000 - 2 * PI * 16 * 20);
    p.setHeight("30 mm");
    p.checkOk();
    CHECK(p.volume() == doctest::Approx(72000 - 2 * PI * 16 * 30).epsilon(1e-9));
}

TEST_CASE("holes at sketch points follow the sketch's face") {
    Plate p;
    auto s = std::make_shared<SketchFeature>();
    s->plane = PlaneRef::onFace(p.topFace());
    const int a = s->sketch.addPoint(15, 20);
    const int b = s->sketch.addPoint(45, 20);
    const FeatureId sid = p.doc.addFeature(s);
    auto h = std::make_shared<HoleFeature>();
    h->sketch = sid;
    h->sketchPoints = {a, b};
    h->diameter = p.doc.makeSlot("5 mm");
    h->depth = p.doc.makeSlot("10 mm");
    h->flatTip = true;
    const FeatureId id = p.doc.addFeature(h);
    p.checkOk();
    CHECK(p.volume() == doctest::Approx(48000 - 2 * PI * 6.25 * 10).epsilon(1e-9));
    p.edit<HoleFeature>(id, [](HoleFeature &x) { x.depth.expr = "12 mm"; });
    CHECK(p.volume() == doctest::Approx(48000 - 2 * PI * 6.25 * 12).epsilon(1e-9));
    p.suppressCycle(id, 48000, 48000 - 2 * PI * 6.25 * 12);
    // Taller: the sketch rides on the top face, the holes stay 12 deep.
    p.setHeight("30 mm");
    p.checkOk();
    CHECK(p.volume() == doctest::Approx(72000 - 2 * PI * 6.25 * 12).epsilon(1e-9));
    const Bnd_Box bb = boundingBox(p.body().shape.shape());
    CHECK(bb.CornerMax().Z() == doctest::Approx(30).epsilon(1e-6));
}

TEST_CASE("combine: cut, keep tools, join, suppress, upstream edit") {
    Plate p; // body A: 60 x 40 x 20
    const FeatureId s2 = p.doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {50, 0}, {80, 40}));
    const FeatureId e2 = p.doc.addFeature(extrudeAll(p.doc, s2, "20 mm")); // body B, overlapping A by 10 x 40 x 20
    REQUIRE(p.bodies() == 2);
    const BodyId a = "b" + std::to_string(p.extrude), b = "b" + std::to_string(e2);
    REQUIRE(p.doc.displayedState()->bodies.count(a));
    REQUIRE(p.doc.displayedState()->bodies.count(b));
    auto c = std::make_shared<CombineFeature>();
    c->target = a;
    c->tools = {b};
    c->operation = BodyOperation::Cut;
    const FeatureId id = p.doc.addFeature(c);
    p.checkOk();
    CHECK(p.bodies() == 1);
    CHECK(p.volume() == doctest::Approx(48000 - 8000).epsilon(1e-9));
    p.edit<CombineFeature>(id, [](CombineFeature &x) { x.keepTools = true; });
    CHECK(p.bodies() == 2);
    CHECK(p.volume() == doctest::Approx(40000 + 24000).epsilon(1e-9));
    p.edit<CombineFeature>(id, [](CombineFeature &x) {
        x.operation = BodyOperation::Join;
        x.keepTools = false;
    });
    CHECK(p.bodies() == 1);
    CHECK(p.volume() == doctest::Approx(48000 + 24000 - 8000).epsilon(1e-9));
    p.suppressCycle(id, 48000 + 24000, 64000);
    // B gets taller: the joined body follows.
    p.setDistance(e2, "30 mm");
    p.checkOk();
    CHECK(p.bodies() == 1);
    CHECK(p.volume() == doctest::Approx(48000 + 36000 - 8000).epsilon(1e-9));
}

TEST_CASE("construction plane: offset, rotated, used by a sketch, upstream edit") {
    Plate p;
    auto pl = std::make_shared<ConstructionPlaneFeature>();
    pl->base = PlaneRef::onFace(p.topFace());
    pl->offset = p.doc.makeSlot("10 mm");
    const FeatureId id = p.doc.addFeature(pl);
    p.checkOk();
    auto plane = [&]() { return p.doc.displayedState()->planes.at(id); };
    CHECK(plane()->frame.Location().Z() == doctest::Approx(30));
    CHECK(plane()->center.Distance(gp_Pnt(30, 20, 30)) < 1e-6); // drawn over the face
    // A boss sketched on the plane, extruded down onto the plate.
    auto s = circleSketch(PlaneRef::construction(id), Vec2{30, 20}, 5);
    const FeatureId sid = p.doc.addFeature(s);
    auto boss = extrudeAll(p.doc, sid, "-10 mm", BodyOperation::Join);
    p.doc.addFeature(boss);
    p.checkOk();
    CHECK(p.bodies() == 1);
    CHECK(p.volume() == doctest::Approx(48000 + PI * 25 * 10).epsilon(1e-9));
    // Offset 15: the 10 mm boss no longer reaches the plate.
    p.edit<ConstructionPlaneFeature>(id, [](ConstructionPlaneFeature &x) { x.offset.expr = "15 mm"; });
    CHECK(plane()->frame.Location().Z() == doctest::Approx(35));
    CHECK(p.bodies() == 2); // the boss now floats 5 mm above the plate: a new body
    // Rotated 90 degrees about its X axis: a vertical plane.
    p.edit<ConstructionPlaneFeature>(id, [&](ConstructionPlaneFeature &x) {
        x.offset.expr = "10 mm";
        x.angle = p.doc.makeSlot("90 deg");
    });
    CHECK(std::fabs(plane()->frame.Direction().Z()) < 1e-9);
    p.edit<ConstructionPlaneFeature>(id, [](ConstructionPlaneFeature &x) { x.angle = ParamSlot{}; });
    CHECK(p.volume() == doctest::Approx(48000 + PI * 25 * 10).epsilon(1e-9));
    // Suppressing the plane leaves its sketch without a plane: an error, not a crash.
    p.doc.setSuppressed(id, true);
    CHECK(p.doc.statusOf(sid).isError());
    CHECK(p.volume() == doctest::Approx(48000).epsilon(1e-9));
    p.doc.setSuppressed(id, false);
    p.checkOk();
    // A taller plate: the plane and the boss ride up with the top face.
    p.setHeight("30 mm");
    p.checkOk();
    CHECK(plane()->frame.Location().Z() == doctest::Approx(40));
    CHECK(p.bodies() == 1);
    CHECK(p.volume() == doctest::Approx(72000 + PI * 25 * 10).epsilon(1e-9));
}

// Drawing more curves in a sketch that an extrude already uses splits its
// region into pieces; the extrude keeps sweeping the whole region, so the body
// does not change. The new pieces are there to extrude as new bodies.
TEST_CASE("curves added to a used sketch do not change the body") {
    Document doc;
    const FeatureId sk = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {40, 20}));
    auto e = extrudeAll(doc, sk, "10 mm");
    REQUIRE(e->profiles.size() == 1);
    captureProfileOutline(*doc.stateAt(doc.indexOf(sk) + 1), e->profiles[0]);
    CHECK(e->profiles[0].outline.size() >= 4);
    const FeatureId ex = doc.addFeature(e);
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(8000).epsilon(1e-9));

    // A line across the rectangle and a circle over its right edge.
    auto s = std::static_pointer_cast<SketchFeature>(doc.feature(sk)->clone());
    s->sketch.addLine(Vec2{15, -5}, Vec2{25, 25});
    s->sketch.addCircle(Vec2{40, 10}, 6);
    doc.replaceFeature(s);
    const auto st = doc.displayedState();
    CHECK(st->sketches.at(sk)->profiles.size() > 3); // split into pieces
    CHECK(st->bodies.size() == 1);
    CHECK(totalVolume(st) == doctest::Approx(8000).epsilon(1e-9));
    CHECK(doc.statusOf(ex).isOk());

    // Saved and loaded: the outline goes with it.
    Document again;
    std::string error;
    REQUIRE(again.fromJson(doc.toJson(), error));
    CHECK(totalVolume(again.displayedState()) == doctest::Approx(8000).epsilon(1e-9));
    const auto ref = ProfileRef::fromJson(e->profiles[0].toJson());
    CHECK(ref.outline.size() == e->profiles[0].outline.size());

    // A piece outside the rectangle (part of the circle) extruded as a new body.
    const Profile *outside = nullptr;
    for(const auto &p : st->sketches.at(sk)->profiles)
        if(p.sample.x > 40) outside = &p;
    REQUIRE(outside);
    auto e2 = std::make_shared<ExtrudeFeature>();
    e2->profiles.push_back({sk, outside->key, outside->sample});
    e2->distance = doc.makeSlot("10 mm");
    doc.addFeature(e2);
    CHECK(doc.displayedState()->bodies.size() == 2);
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(8000 + PI * 36 / 2 * 10).epsilon(1e-6));

    // Old designs (no outline) keep the previous behaviour: one piece.
    ProfileRef old{sk, "no-such-key", Vec2{5, 5}};
    const SketchResult *sr = nullptr;
    Status status;
    CHECK(resolveProfiles(*doc.displayedState(), old, sr, status).size() == 1);
}
