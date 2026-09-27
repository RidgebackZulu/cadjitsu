// Draft: faces tilted about a hinge edge.
#include <doctest.h>

#include "TestModels.h"
#include "features/DraftFeature.h"

#include <cmath>

using namespace cadtest;

namespace {

struct Setup {
    Document doc;
    FeatureId ex = 0;
    Setup() {
        const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {40, 20}));
        ex = doc.addFeature(extrudeAll(doc, s, "10"));
    }
    const Body &body() { return *onlyBody(doc.displayedState()); }
    TopoRef face(gp_Dir n) { return makeTopoRef(body(), TopoKind::Face, planarFaceWithNormal(body(), n)); }
    TopoRef edge(gp_Pnt mid) { return makeTopoRef(body(), TopoKind::Edge, edgeNear(body(), mid)); }
};

double tanDeg(double d) { return std::tan(d * M_PI / 180.0); }

} // namespace

TEST_CASE("draft a wall about its foot: it leans in, the foot stays") {
    Setup s;
    auto d = std::make_shared<DraftFeature>();
    d->faces = {s.face(gp_Dir(1, 0, 0))};
    d->hinge = s.edge(gp_Pnt(40, 10, 0)); // the +X wall's bottom edge
    d->angle = s.doc.makeSlot("10 deg");
    s.doc.addFeature(d);
    INFO(s.doc.statusOf(d->id).message);
    REQUIRE(s.doc.statusOf(d->id).isOk());
    const StatePtr st = s.doc.displayedState();
    // A wedge 10 x 10*tan(10) x 20 comes off.
    CHECK(totalVolume(st) == doctest::Approx(8000 - 0.5 * 10 * 10 * tanDeg(10) * 20).epsilon(1e-6));
    const Bnd_Box b = boundingBox(onlyBody(st)->shape.shape());
    double x0, y0, z0, x1, y1, z1;
    b.Get(x0, y0, z0, x1, y1, z1);
    CHECK(x1 == doctest::Approx(40).epsilon(1e-6)); // the foot did not move
    // Flip leans it out instead.
    auto flipped = std::static_pointer_cast<DraftFeature>(d->clone());
    flipped->flip = true;
    s.doc.replaceFeature(flipped);
    CHECK(totalVolume(s.doc.displayedState()) == doctest::Approx(8000 + 0.5 * 10 * 10 * tanDeg(10) * 20).epsilon(1e-6));
}

TEST_CASE("draft all four walls of a box about one bottom edge: a tapered box") {
    Setup s;
    auto d = std::make_shared<DraftFeature>();
    d->faces = {s.face(gp_Dir(1, 0, 0)), s.face(gp_Dir(-1, 0, 0)), s.face(gp_Dir(0, 1, 0)), s.face(gp_Dir(0, -1, 0))};
    d->hinge = s.edge(gp_Pnt(40, 10, 0));
    d->angle = s.doc.makeSlot("5 deg");
    s.doc.addFeature(d);
    INFO(s.doc.statusOf(d->id).message);
    REQUIRE(s.doc.statusOf(d->id).isOk());
    // A frustum: bottom 40 x 20, top shrunk by 10*tan(5) on every side.
    const double t = 10 * tanDeg(5);
    const double a1 = 40 * 20, a2 = (40 - 2 * t) * (20 - 2 * t);
    const double frustum = 10.0 / 3.0 * (a1 + a2 + std::sqrt(a1 * a2));
    CHECK(totalVolume(s.doc.displayedState()) == doctest::Approx(frustum).epsilon(1e-3));
    // It still prints: a single watertight solid.
    CHECK(onlyBody(s.doc.displayedState()) != nullptr);
}

TEST_CASE("a hinge that is not on the face is an error") {
    Setup s;
    auto d = std::make_shared<DraftFeature>();
    d->faces = {s.face(gp_Dir(1, 0, 0))};
    d->hinge = s.edge(gp_Pnt(0, 10, 0)); // the -X wall's foot
    d->angle = s.doc.makeSlot("10 deg");
    s.doc.addFeature(d);
    CHECK(s.doc.statusOf(d->id).isError());
}
