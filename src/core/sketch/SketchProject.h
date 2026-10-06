#pragma once

#include "base/Ids.h"
#include "sketch/Sketch.h"
#include "sketch/SketchResult.h"

#include <gp_Ax3.hxx>

#include <functional>
#include <string>
#include <vector>

namespace cad {

// Geometry projected from another sketch (usually one on another plane) onto
// a sketch: each projected curve and point keeps a link to the entity it came
// from (SkEntity::projSketch / projEntity), follows it when it changes, and
// is fixed in this sketch. Projected geometry is otherwise ordinary: it makes
// profiles, and can be made construction geometry and back.
//
// Lines and points project onto any plane; circles and arcs only onto a plane
// parallel to theirs (on a tilted one they would be ellipses).

// Projects `entityId` of `source` onto `target` (whose plane is `frame`). A
// curve's points come too; points already projected from the same source
// points are reused, so projected curves stay joined. Projecting something
// projected before does nothing (and succeeds). `created` lists the new
// entities.
bool projectEntity(Sketch &target, const gp_Ax3 &frame, const SketchResult &source, int entityId,
                   std::vector<int> &created, std::string &error);

// Brings projected geometry up to date with its sources. `sourceOf` finds a
// sketch by feature id (null: gone). Geometry whose source is gone keeps its
// last place and adds a warning.
void refreshProjections(Sketch &target, const gp_Ax3 &frame,
                        const std::function<const SketchResult *(FeatureId)> &sourceOf,
                        std::vector<std::string> &warnings);

// The sketches `target` projects geometry from.
std::vector<FeatureId> projectionSources(const Sketch &target);

// Whether a curve of `source` can be projected onto a plane `frame`.
bool canProject(const SketchResult &source, const SkEntity &e, const gp_Ax3 &frame, std::string *why = nullptr);

} // namespace cad
