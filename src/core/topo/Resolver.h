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

// The region a profile reference means now: the profile itself, or, if
// curves added to the sketch since split it, every piece of it (found by the
// reference's stored outline). Falls back to resolveProfile().
std::vector<const Profile *> resolveProfiles(const ModelState &state, const ProfileRef &ref, const SketchResult *&sketch,
                                             Status &status);

// Stores the shape of the region `ref` names in `state` (see ProfileRef::outline),
// if it resolves exactly by key; leaves it as it is otherwise.
void captureProfileOutline(const ModelState &state, ProfileRef &ref);

// Standard frames of the origin planes (Z up).
gp_Ax3 originPlaneFrame(PlaneRef::Kind kind);

} // namespace cad
