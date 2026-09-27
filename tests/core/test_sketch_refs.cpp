// Extrudes keep finding their sketch regions after the sketch is edited.
#include <doctest.h>

#include "TestModels.h"
#include "features/SketchRefs.h"

using namespace cadtest;

TEST_CASE("profile references follow a region moved and then re-cut") {
    Document doc;
    auto sk = rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {20, 10});
    const FeatureId sid = doc.addFeature(sk);
    auto ex = extrudeAll(doc, sid, "5");
    const FeatureId eid = doc.addFeature(ex);
    REQUIRE(doc.statusOf(eid).isOk());

    // Move the rectangle far away (same curves, same key).
    auto moved = std::static_pointer_cast<SketchFeature>(doc.feature(sid)->clone());
    for(SkEntity &e : moved->sketch.entities)
        if(e.type == SkType::Point && e.id > 0) e.x += 100;
    doc.replaceFeature(moved, "Edit Sketch1");
    CHECK(refreshProfileRefs(doc, sid) == 1);
    auto e1 = std::dynamic_pointer_cast<const ExtrudeFeature>(doc.feature(eid));
    REQUIRE(e1);
    CHECK(e1->profiles[0].sample.x > 99); // looked for where the region is now

    // Then a line across it renumbers its curve pieces: the region is still
    // found (both halves, by the refreshed outline) and the solid is unchanged.
    auto cut = std::static_pointer_cast<SketchFeature>(doc.feature(sid)->clone());
    cut->sketch.addLine(Vec2{110, -5}, Vec2{110, 15});
    doc.replaceFeature(cut, "Edit Sketch1");
    const StatePtr s = doc.displayedState();
    CHECK(doc.statusOf(eid).severity != Severity::Error);
    CHECK(totalVolume(s) == doctest::Approx(20 * 10 * 5));
    const Bnd_Box b = boundingBox(s->bodies.begin()->second->shape.shape());
    double x0, y0, z0, x1, y1, z1;
    b.Get(x0, y0, z0, x1, y1, z1);
    CHECK(x0 == doctest::Approx(100).epsilon(1e-6));
    // Undo takes back the edit and the refresh together.
    doc.undo();
    doc.undo();
    auto back = std::dynamic_pointer_cast<const ExtrudeFeature>(doc.feature(eid));
    CHECK(back->profiles[0].sample.x < 20);
}
