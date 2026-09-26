// Helpers for building documents in tests through the same Document API the UI uses.
#pragma once

#include "doc/Document.h"
#include "features/ChamferFeature.h"
#include "features/CombineFeature.h"
#include "features/ConstructionPlaneFeature.h"
#include "features/ExtrudeFeature.h"
#include "features/FilletFeature.h"
#include "features/HoleFeature.h"
#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Surface.hxx>
#include <TopoDS.hxx>

#include <cmath>
#include <functional>
#include <memory>

namespace cadtest {

using namespace cad;

inline std::shared_ptr<SketchFeature> rectSketch(PlaneRef plane, Vec2 a, Vec2 b) {
    auto s = std::make_shared<SketchFeature>();
    s->plane = plane;
    s->sketch.addRectangle(a, b);
    return s;
}

inline std::shared_ptr<SketchFeature> circleSketch(PlaneRef plane, Vec2 c, double r) {
    auto s = std::make_shared<SketchFeature>();
    s->plane = plane;
    s->sketch.addCircle(c, r);
    return s;
}

// Extrudes every profile of `sketchId`.
inline std::shared_ptr<ExtrudeFeature> extrudeAll(Document &doc, FeatureId sketchId, const std::string &dist,
                                                  BodyOperation op = BodyOperation::NewBody) {
    auto e = std::make_shared<ExtrudeFeature>();
    const auto state = doc.stateAt(doc.indexOf(sketchId) + 1);
    const auto &sk = *state->sketches.at(sketchId);
    for(const auto &p : sk.profiles) e->profiles.push_back({sketchId, p.key, p.sample});
    e->distance = doc.makeSlot(dist);
    e->operation = op;
    return e;
}

inline double totalVolume(const StatePtr &s) {
    double v = 0;
    for(const auto &kv : s->bodies) v += volumeOf(kv.second->shape.shape());
    return v;
}

inline const Body *onlyBody(const StatePtr &s) { return s->bodies.size() == 1 ? s->bodies.begin()->second.get() : nullptr; }

// First face of `body` satisfying `pred`.
inline int findFace(const Body &body, const std::function<bool(const TopoDS_Face &)> &pred) {
    for(int i = 1; i <= body.shape.faceCount(); ++i)
        if(pred(body.shape.face(i))) return i;
    return 0;
}

// Planar face whose outward normal is `n` (and optionally whose centroid has z == zc).
inline int planarFaceWithNormal(const Body &body, gp_Dir n) {
    return findFace(body, [&](const TopoDS_Face &f) {
        gp_Pln p;
        return planeOfFace(f, p) && p.Axis().Direction().IsEqual(n, 1e-6);
    });
}

// Linear edge whose midpoint is near `p`.
inline int edgeNear(const Body &body, gp_Pnt p, double tol = 1e-3) {
    for(int i = 1; i <= body.shape.edgeCount(); ++i)
        if(body.shape.isSelectableEdge(i) && midpointOfEdge(body.shape.edge(i)).Distance(p) < tol) return i;
    return 0;
}

} // namespace cadtest
