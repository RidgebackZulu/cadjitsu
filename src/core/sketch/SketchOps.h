#pragma once

#include "sketch/Sketch.h"

#include <string>
#include <vector>

namespace cad {

// Sketch operations that make new geometry from existing geometry. Each
// changes `sketch` in place and lists the entities it made in `created`; on
// failure it leaves `sketch` alone and says why in `error`.
//
// `ids` are the entities to copy: curves (with their points), texts and lone
// points. A point copied goes with every curve using it.

// Mirror images of `ids` about a line (a line entity, or the sketch X / Y
// axis: kSketchXAxis / kSketchYAxis). Each copied point is held symmetric to
// its original about the line (and mirrored circles keep the same radius),
// so the mirror image follows when the original changes. Points on the
// mirror line are shared rather than copied, so a half profile drawn up to
// the line becomes one closed profile.
bool mirrorEntities(Sketch &sketch, const std::vector<int> &ids, int lineId, std::vector<int> &created,
                    std::string &error);

// `count` - 1 rotated copies of `ids` around `centre`, spread evenly over
// `totalAngle` degrees (360: all the way round, the last copy one step before
// the first). Constraints between the copied entities are copied too (their
// dimensions follow the originals'); points at the centre are shared.
bool patternEntities(Sketch &sketch, const std::vector<int> &ids, Vec2 centre, int count, double totalAngle,
                     std::vector<int> &created, std::string &error);

// The rotation step of a circular pattern (degrees).
double patternStep(int count, double totalAngle);

} // namespace cad
