#pragma once

#include "base/Status.h"
#include "doc/ModelState.h"
#include "topo/Refs.h"

#include <gp_Ax3.hxx>

namespace cad {

struct ResolvedRef {
    bool ok = false;
    const Body *body = nullptr;
    int index = 0;         // index into body->shape for the ref's kind
    TopoDS_Shape shape;
    Status status;         // Warning when re-resolved by similarity, Error when lost
};

// Finds the entity a stored reference points to in `state`: by exact name,
// then alias, then base name (closest signature), then pure geometry.
ResolvedRef resolveRef(const ModelState &state, const TopoRef &ref);

// Builds a reference to entity `index` of `body`.
TopoRef makeTopoRef(const Body &body, TopoKind kind, int index);

// Sketch / construction plane placement. Face planes use the outward normal.
bool resolvePlane(const ModelState &state, const PlaneRef &ref, gp_Ax3 &frame, Status &status);

// Finds a sketch profile by key, falling back to the region containing the
// stored sample point, or the most similar set of bounding segments.
const Profile *resolveProfile(const ModelState &state, const ProfileRef &ref, const SketchResult *&sketch,
                              Status &status);

// Standard frames of the origin planes (Z up).
gp_Ax3 originPlaneFrame(PlaneRef::Kind kind);

} // namespace cad
