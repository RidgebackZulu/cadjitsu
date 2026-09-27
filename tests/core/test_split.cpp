// Split Body: by planes and sketch curves, keeping one side, alignment pins.
#include <doctest.h>

#include "TestModels.h"
#include "features/SplitFeature.h"
#include "io/StlWriter.h"

using namespace cadtest;

namespace {

void requireOk(Document &doc) {
    for(const auto &f : doc.features()) {
        INFO(f->name << ": " << doc.statusOf(f->id).message);
        REQUIRE(doc.statusOf(f->id).severity != Severity::Error);
    }
}

// A 40 x 20 x 10 box from the origin.
FeatureId box(Document &doc) {
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {40, 20}));
    doc.addFeature(extrudeAll(doc, s, "10"));
    return s;
}

std::shared_ptr<SplitFeature> byPlane(Document &doc, PlaneRef p) {
    auto sp = std::make_shared<SplitFeature>();
    sp->plane = p;
    sp->pinDiameter = doc.makeSlot("3.2 mm");
    sp->pinDepth = doc.makeSlot("6 mm");
    return sp;
}

} // namespace

TEST_CASE("split a body with a construction plane") {
    Document doc;
    box(doc);
    auto p = std::make_shared<ConstructionPlaneFeature>();
    p->base = PlaneRef::origin(PlaneRef::Kind::YZ);
    p->offset = doc.makeSlot("15 mm");
    const FeatureId pid = doc.addFeature(p);
    doc.addFeature(byPlane(doc, PlaneRef::construction(pid)));
    requireOk(doc);
    const StatePtr s = doc.displayedState();
    REQUIRE(s->bodies.size() == 2);
    CHECK(totalVolume(s) == doctest::Approx(8000));
    // The biggest piece keeps the body's name.
    const Body *big = s->body("b2");
    REQUIRE(big);
    CHECK(big->name == "Body1");
    CHECK(volumeOf(big->shape.shape()) == doctest::Approx(25 * 20 * 10));
    // It survives a save and load.
    Document again;
    std::string error;
    REQUIRE(again.fromJson(doc.toJson(), error));
    CHECK(again.displayedState()->bodies.size() == 2);
}

TEST_CASE("split keeps only the chosen side") {
    Document doc;
    box(doc);
    auto sp = byPlane(doc, PlaneRef::origin(PlaneRef::Kind::XY));
    // The XY plane only touches the bottom: nothing to cut, a warning.
    doc.addFeature(sp);
    CHECK(doc.statusOf(sp->id).severity != Severity::Ok);
    Document doc2;
    box(doc2);
    auto p = std::make_shared<ConstructionPlaneFeature>();
    p->base = PlaneRef::origin(PlaneRef::Kind::XY);
    p->offset = doc2.makeSlot("4 mm");
    const FeatureId pid = doc2.addFeature(p);
    auto front = byPlane(doc2, PlaneRef::construction(pid));
    front->keep = SplitKeep::Front; // above z = 4
    doc2.addFeature(front);
    requireOk(doc2);
    const StatePtr s = doc2.displayedState();
    REQUIRE(s->bodies.size() == 1);
    CHECK(totalVolume(s) == doctest::Approx(40 * 20 * 6));
}

TEST_CASE("split along an angled sketch line") {
    Document doc;
    box(doc);
    auto sk = std::make_shared<SketchFeature>();
    sk->plane = PlaneRef::origin(PlaneRef::Kind::XY);
    sk->sketch.addLine(Vec2{10, -5}, Vec2{30, 25}); // crosses the box corner to corner-ish
    const FeatureId sid = doc.addFeature(sk);
    auto sp = std::make_shared<SplitFeature>();
    sp->tool = SplitTool::Sketch;
    sp->sketch = sid;
    doc.addFeature(sp);
    requireOk(doc);
    const StatePtr s = doc.displayedState();
    REQUIRE(s->bodies.size() == 2);
    CHECK(totalVolume(s) == doctest::Approx(8000));
    // The line x = 10 + (y + 5) * 2/3 splits the 40 x 20 footprint into areas 400 and 400.
    for(const auto &kv : s->bodies) CHECK(volumeOf(kv.second->shape.shape()) == doctest::Approx(4000).epsilon(1e-6));
}

TEST_CASE("split with alignment pin holes in both halves, and they print") {
    Document doc;
    box(doc);
    auto p = std::make_shared<ConstructionPlaneFeature>();
    p->base = PlaneRef::origin(PlaneRef::Kind::YZ);
    p->offset = doc.makeSlot("20 mm");
    const FeatureId pid = doc.addFeature(p);
    auto sp = byPlane(doc, PlaneRef::construction(pid));
    sp->pins = true;
    doc.addFeature(sp);
    requireOk(doc);
    const StatePtr s = doc.displayedState();
    REQUIRE(s->bodies.size() == 2);
    // Two 3.2 mm holes, 6 mm deep, in each half.
    const double hole = M_PI * 1.6 * 1.6 * 6;
    CHECK(totalVolume(s) == doctest::Approx(8000 - 4 * hole).epsilon(1e-6));
    for(const auto &kv : s->bodies) {
        CHECK(volumeOf(kv.second->shape.shape()) == doctest::Approx(4000 - 2 * hole).epsilon(1e-6));
        StlOptions opt;
        StlExport ex;
        std::string err;
        REQUIRE(buildStlMesh({kv.second->shape.shape()}, opt, ex, err));
        INFO(ex.report.summary());
        CHECK(ex.report.ok);
        CHECK(ex.report.shells == 1);
    }
    // The pins are parameters.
    bool found = false;
    for(const ParamDef &d : sp->params()) found |= d.label.find("Pin Diameter") != std::string::npos;
    CHECK(found);
}
