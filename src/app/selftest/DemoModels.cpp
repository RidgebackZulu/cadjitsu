#include "selftest/DemoModels.h"

#include "features/ExtrudeFeature.h"
#include "features/FilletFeature.h"
#include "features/HoleFeature.h"
#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <TopoDS.hxx>

namespace cadjitsu {

using namespace cad;

namespace {

FeatureId addSketch(Document &doc, PlaneRef plane, const std::function<void(Sketch &)> &draw) {
    auto s = std::make_shared<SketchFeature>();
    s->plane = plane;
    draw(s->sketch);
    return doc.addFeature(s);
}

FeatureId extrudeAll(Document &doc, FeatureId sketch, const std::string &dist, BodyOperation op) {
    auto e = std::make_shared<ExtrudeFeature>();
    const auto st = doc.stateAt(doc.indexOf(sketch) + 1);
    for(const auto &p : st->sketches.at(sketch)->profiles) e->profiles.push_back({sketch, p.key, p.sample});
    e->distance = doc.makeSlot(dist);
    e->operation = op;
    return doc.addFeature(e);
}

int faceWithNormal(const Body &b, gp_Dir n, double z) {
    for(int i = 1; i <= b.shape.faceCount(); ++i) {
        gp_Pln p;
        if(planeOfFace(b.shape.face(i), p) && p.Axis().Direction().IsEqual(n, 1e-6) &&
           std::fabs(p.Location().Z() - z) < 1e-6)
            return i;
    }
    return 0;
}

} // namespace

void buildDemoBracket(Document &doc) {
    const FeatureId base = addSketch(doc, PlaneRef::origin(PlaneRef::Kind::XY), [](Sketch &s) {
        s.addRectangle({0, 0}, {60, 40});
    });
    extrudeAll(doc, base, "12 mm", BodyOperation::NewBody);

    auto state = doc.displayedState();
    const Body &plate = *state->bodies.begin()->second;
    const int top = faceWithNormal(plate, gp_Dir(0, 0, 1), 12);
    const TopoRef topRef = makeTopoRef(plate, TopoKind::Face, top);

    // Boss on the top face.
    const FeatureId boss = addSketch(doc, PlaneRef::onFace(topRef), [](Sketch &s) { s.addCircle(Vec2{45, 20}, 8); });
    extrudeAll(doc, boss, "10 mm", BodyOperation::Join);

    // Counterbored hole through the plate and a plain through hole.
    auto h = std::make_shared<HoleFeature>();
    h->face = topRef;
    h->points = {{14, 20}};
    h->holeType = HoleType::Counterbore;
    h->extent = ExtentType::ThroughAll;
    h->flatTip = true;
    h->diameter = doc.makeSlot("6.5 mm");
    h->depth = doc.makeSlot("12 mm");
    h->cboreDiameter = doc.makeSlot("11 mm");
    h->cboreDepth = doc.makeSlot("6.5 mm");
    h->tipAngle = doc.makeSlot("118 deg");
    doc.addFeature(h);

    auto h2 = std::make_shared<HoleFeature>();
    h2->face = topRef;
    h2->points = {{45, 20}};
    h2->extent = ExtentType::ThroughAll;
    h2->flatTip = true;
    h2->diameter = doc.makeSlot("5 mm");
    h2->depth = doc.makeSlot("10 mm");
    h2->tipAngle = doc.makeSlot("118 deg");
    doc.addFeature(h2);

    // Fillet the two vertical corner edges at x = 60.
    state = doc.displayedState();
    const Body &b = *state->bodies.begin()->second;
    auto f = std::make_shared<FilletFeature>();
    for(int i = 1; i <= b.shape.edgeCount(); ++i) {
        if(!b.shape.isSelectableEdge(i)) continue;
        const gp_Pnt m = midpointOfEdge(b.shape.edge(i));
        if(std::fabs(m.X() - 60) < 1e-6 && std::fabs(m.Z() - 6) < 1e-6) f->edges.push_back(makeTopoRef(b, TopoKind::Edge, i));
    }
    f->radius = doc.makeSlot("6 mm");
    doc.addFeature(f);
}

} // namespace cadjitsu
