#pragma once

#include "geom/Align.h"
#include "sketch/Sketch.h"

#include <optional>

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

// Moves `ids` (curves with their points, texts, lone points): each point p
// goes to pivot + R(angle) (p - pivot) + delta, texts turn with them. Pins
// on moved points are dropped, and horizontal / vertical on turned lines
// (swapped by a quarter turn). The sketch is not solved.
void transformEntities(Sketch &sketch, const std::vector<int> &ids, Vec2 pivot, Vec2 delta, double angle);

// Align in a sketch: a pick on the geometry that moves (from) and one it goes
// to (to): a point, a line (the end nearer `click`, or its middle), a circle
// or arc (its centre), the origin (kSketchOrigin) or an axis (kSketchXAxis,
// kSketchYAxis). The move it makes (see alignTransform: point onto point or
// line, line along line, `flip`, then turned `angle` radians), as a pivot,
// a shift and a turn for transformEntities / the editor's move; false with
// `why` if it cannot.
struct SketchAlignPick {
    int id = 0;
    std::optional<Vec2> click;
};
struct SketchAlignMove {
    Vec2 pivot, delta;
    double angle = 0;
    AlignKind from = AlignKind::Point, to = AlignKind::Point;
};
std::optional<SketchAlignMove> sketchAlignMove(const Sketch &sketch, const SketchAlignPick &from, const SketchAlignPick &to,
                                               bool flip, double angle, std::string &why);
// The kind a pick stands for (nullopt: none, e.g. a text).
std::optional<AlignKind> sketchAlignKind(const Sketch &sketch, int id);

} // namespace cad
