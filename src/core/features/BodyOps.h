#pragma once

#include "base/Status.h"
#include "doc/ModelState.h"
#include "topo/NamedShape.h"

#include <string>
#include <vector>

namespace cad {

enum class BoolOp { Fuse, Cut, Common };

// Boolean with topological naming. Runs in non-destructive mode (inputs are
// shared, immutable bodies), simplifies the result (merges coplanar faces)
// and propagates face names through the combined history.
struct BooleanResult {
    bool ok = false;
    std::string error;
    NamedShape shape;
    Handle(BRepTools_History) history;
};

BooleanResult runBoolean(BoolOp op, const std::vector<const NamedShape *> &args,
                         const std::vector<const NamedShape *> &tools, const std::string &prefix);

// Merges coplanar / cocylindrical faces of a single shape, keeping names.
NamedShape unifyNamed(const NamedShape &shape, const std::string &prefix);

// Adds one new body per solid of `shape` (ids "b<fid>", "b<fid>.2", ... by
// decreasing volume; default names "BodyN").
std::vector<BodyId> addNewBodies(ModelState &state, const NamedShape &shape, FeatureId fid);

// Replaces body `id` with the solids of `result`: the largest keeps the id,
// others become "<id>.2", "<id>.3"... Removes the body if `result` is empty.
std::vector<BodyId> replaceBody(ModelState &state, const BodyId &id, const NamedShape &result);

// Checks and, if needed, repairs a solid result. Returns false if unusable.
bool validateResult(NamedShape &shape, const std::string &prefix, Status &status);

// Bodies whose shape intersects (or, if `touching`, touches) `tool`.
std::vector<BodyId> bodiesInteracting(const ModelState &state, const TopoDS_Shape &tool, bool touching);

} // namespace cad
