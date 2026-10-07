// Revolve (solids of revolution) and Shell (hollowing with even walls).
#include <doctest.h>

#include "TestModels.h"
#include "features/RevolveFeature.h"
#include "features/ShellFeature.h"

#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <cmath>

using namespace cadtest;

namespace {

std::shared_ptr<RevolveFeature> revolveAll(Document &doc, FeatureId sketchId, const std::string &angle) {
    auto r = std::make_shared<RevolveFeature>();
    const auto state = doc.stateAt(doc.indexOf(sketchId) + 1);
    for(const auto &p : state->sketches.at(sketchId)->profiles) r->profiles.push_back({sketchId, p.key, p.sample});
    r->angle = doc.makeSlot(angle);
    return r;
}

int faceCount(const Body &b) {
    TopTools_IndexedMapOfShape m;
    TopExp::MapShapes(b.shape.shape(), TopAbs_FACE, m);
    return m.Extent();
}

} // namespace

TEST_CASE("revolve: a rectangle off the axis makes a tube; part of a turn a sector") {
    Document doc;
    // On XZ, x 5..10, z 0..20, turned about the world Z axis: a tube.
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XZ), {5, 0}, {10, 20}));
    auto r = revolveAll(doc, s, "360 deg");
    r->axis.axis.builtin = "z";
    doc.addFeature(r);
    INFO(doc.statusOf(r->id).message);
    REQUIRE(doc.statusOf(r->id).isOk());
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(kPi * (100 - 25) * 20).epsilon(1e-6));
    CHECK(onlyBody(doc.displayedState()) != nullptr);
    // A quarter turn: a quarter of the tube, with start and end faces.
    auto quarter = std::static_pointer_cast<RevolveFeature>(r->clone());
    quarter->angle = doc.makeSlot("90 deg");
    doc.replaceFeature(quarter);
    REQUIRE(doc.statusOf(r->id).isOk());
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(kPi * 75 * 20 / 4).epsilon(1e-6));
    CHECK(faceCount(*onlyBody(doc.displayedState())) == 6);
    // Symmetric: 90 each way.
    quarter->extent = RevolveExtent::Symmetric;
    doc.replaceFeature(std::static_pointer_cast<RevolveFeature>(quarter->clone()));
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(kPi * 75 * 20 / 2).epsilon(1e-6));
    // Round trip.
    const json j = doc.feature(r->id)->toJson();
    auto back = std::static_pointer_cast<RevolveFeature>(Feature::fromJson(j));
    REQUIRE(back);
    CHECK(back->axis.axis.builtin == "z");
    CHECK((back->extent == RevolveExtent::Symmetric));
}

TEST_CASE("revolve: about a sketch line, as a cut, and an axis through the profile is refused") {
    Document doc;
    auto sk = std::make_shared<SketchFeature>();
    sk->plane = PlaneRef::origin(PlaneRef::Kind::XZ);
    sk->sketch.addRectangle({0, 0}, {10, 30});           // touches the axis: a solid cylinder
    const int axisLine = sk->sketch.addLine(Vec2(0, -5), Vec2(0, 40), true);
    const FeatureId s = doc.addFeature(sk);
    auto r = revolveAll(doc, s, "360 deg");
    r->axis.sketch = s;
    r->axis.line = axisLine;
    doc.addFeature(r);
    INFO(doc.statusOf(r->id).message);
    REQUIRE(doc.statusOf(r->id).isOk());
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(kPi * 100 * 30).epsilon(1e-6));
    CHECK((doc.feature(r->id)->dependencies() == std::vector<FeatureId>{s}));

    // A ring groove cut into it: a 2 x 2 square at x 8..10 turned about the same axis.
    auto groove = std::make_shared<SketchFeature>();
    groove->plane = PlaneRef::origin(PlaneRef::Kind::XZ);
    groove->sketch.addRectangle({8, 14}, {12, 16});
    const FeatureId g = doc.addFeature(groove);
    auto cut = revolveAll(doc, g, "360 deg");
    cut->axis.axis.builtin = "z";
    cut->operation = BodyOperation::Cut;
    doc.addFeature(cut);
    REQUIRE(doc.statusOf(cut->id).isOk());
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(kPi * 100 * 30 - kPi * (100 - 64) * 2).epsilon(1e-6));

    // A profile straddling the axis cannot be revolved.
    Document bad;
    const FeatureId b = bad.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XZ), {-5, 0}, {5, 10}));
    auto rb = revolveAll(bad, b, "360 deg");
    rb->axis.axis.builtin = "z";
    bad.addFeature(rb);
    CHECK(bad.statusOf(rb->id).isError());
    CHECK(bad.statusOf(rb->id).message.find("crosses the axis") != std::string::npos);
}

TEST_CASE("shell: an open-top box keeps its outside; a body alone gets a closed void") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {40, 30}));
    const auto ex = extrudeAll(doc, s, "20");
    doc.addFeature(ex);
    const Body &box = *onlyBody(doc.displayedState());
    auto sh = std::make_shared<ShellFeature>();
    sh->faces = {makeTopoRef(box, TopoKind::Face, planarFaceWithNormal(box, gp_Dir(0, 0, 1)))};
    sh->thickness = doc.makeSlot("2");
    doc.addFeature(sh);
    INFO(doc.statusOf(sh->id).message);
    REQUIRE(doc.statusOf(sh->id).isOk());
    // Inside: 36 x 26 x 18 taken out.
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(40 * 30 * 20 - 36 * 26 * 18).epsilon(1e-6));
    CHECK(faceCount(*onlyBody(doc.displayedState())) == 11); // 5 outside, 5 inside, the rim
    // Thicker walls.
    auto thick = std::static_pointer_cast<ShellFeature>(sh->clone());
    thick->thickness = doc.makeSlot("3");
    doc.replaceFeature(thick);
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(24000 - 34 * 24 * 17).epsilon(1e-6));
    // Outside: the inside keeps the box's size and the walls grow outwards.
    auto out = std::static_pointer_cast<ShellFeature>(thick->clone());
    out->thickness = doc.makeSlot("2");
    out->direction = ShellDirection::Outside;
    doc.replaceFeature(out);
    INFO(doc.statusOf(sh->id).message);
    REQUIRE(doc.statusOf(sh->id).isOk());
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(44 * 34 * 22 - 24000).epsilon(1e-6));
    // Too thick.
    auto tooThick = std::static_pointer_cast<ShellFeature>(sh->clone());
    tooThick->thickness = doc.makeSlot("20");
    doc.replaceFeature(tooThick);
    CHECK(doc.statusOf(sh->id).isError());
    // Thin walls warn.
    auto thin = std::static_pointer_cast<ShellFeature>(sh->clone());
    thin->thickness = doc.makeSlot("0.5");
    doc.replaceFeature(thin);
    CHECK(doc.statusOf(sh->id).severity == Severity::Warning);

    // A whole body, no faces: a sealed hollow.
    Document closed;
    const FeatureId cs = closed.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {20, 20}));
    closed.addFeature(extrudeAll(closed, cs, "20"));
    auto hollow = std::make_shared<ShellFeature>();
    hollow->bodies = {onlyBody(closed.displayedState())->id};
    hollow->thickness = closed.makeSlot("2");
    closed.addFeature(hollow);
    INFO(closed.statusOf(hollow->id).message);
    REQUIRE(closed.statusOf(hollow->id).isOk());
    CHECK(totalVolume(closed.displayedState()) == doctest::Approx(8000 - 16 * 16 * 16).epsilon(1e-6));
    // Round trip.
    auto back = std::static_pointer_cast<ShellFeature>(Feature::fromJson(closed.feature(hollow->id)->toJson()));
    REQUIRE(back);
    CHECK(back->bodies == hollow->bodies);
}
