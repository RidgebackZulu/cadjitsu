#pragma once

#include "sketch/Sketch.h"

#include <string>
#include <vector>

namespace cad {

// Sketch edits that reshape curves (Trim, Extend, corner Fillet) and the
// compound shapes (Slot, regular Polygon). Each changes `sketch` in place; on
// failure it leaves `sketch` alone and says why in `error`. `created` lists
// the new entities (points included).

// Removes the piece of curve `curveId` around `pick`, between the nearest
// intersections with the other curves (construction ones too). A line or arc
// is shortened or split in two; a circle becomes an arc. New ends are held on
// the curve that cut them (or share its end point). A curve nothing crosses
// is deleted. Dimensions of the curve's length go (it is no longer that
// long); its other constraints stay with the pieces.
bool trimCurve(Sketch &sketch, int curveId, Vec2 pick, std::vector<int> &created, std::string &error);

// The piece trimCurve would remove, as a polyline (empty: the whole curve, or
// nothing to trim when `curveId` is not a curve).
std::vector<Vec2> trimPreview(const Sketch &sketch, int curveId, Vec2 pick, bool &wholeCurve);

// Lengthens line or arc `curveId` at its end nearer `pick` up to the nearest
// curve in that direction; the end is then held on that curve. The end must
// be free (no other curve uses it).
bool extendCurve(Sketch &sketch, int curveId, Vec2 pick, std::string &error);

// Where extendCurve would take that end: the polyline from the end to there.
std::vector<Vec2> extendPreview(const Sketch &sketch, int curveId, Vec2 pick);

// Rounds the corner where two lines meet at point `corner` with a tangent
// arc of `radius`. The lines are cut back to the arc; the corner point stays
// as a construction point on both lines (a "virtual sharp"), and dimensions
// of the lines' lengths are moved onto it, so they keep their meaning.
// `arcId` is the new arc.
bool filletCorner(Sketch &sketch, int corner, double radius, int &arcId, std::vector<int> &created,
                  std::string &error);

// The corner point of two lines nearest `p` within `maxDistance` (0: none).
int filletCornerNear(const Sketch &sketch, Vec2 p, double maxDistance);

// The arc filletCorner would make (centre, start, end; empty if it cannot).
std::vector<Vec2> filletPreview(const Sketch &sketch, int corner, double radius);

// A slot between centre points `c1` and `c2` (existing points), `width`
// wide: two lines and two half circles, tangent and of equal radius, around
// a construction line joining the centres.
struct SlotIds {
    int centreLine = 0, arc1 = 0, arc2 = 0, line1 = 0, line2 = 0;
};
bool addSlot(Sketch &sketch, int c1, int c2, double width, SlotIds &out, std::string &error);

// A regular polygon of `sides` around centre point `centre` (an existing
// point). Inscribed: `vertex` is a corner, and the corners lie on a
// construction circle. Circumscribed: `vertex` is the middle of an edge, and
// the edges touch the circle (its diameter is then the width across flats).
// The sides are held equal.
struct PolygonIds {
    int circle = 0;
    std::vector<int> lines;
};
bool addRegularPolygon(Sketch &sketch, int centre, Vec2 vertex, int sides, bool inscribed, PolygonIds &out,
                       std::string &error);
// The polygon's corners (for previews).
std::vector<Vec2> regularPolygonCorners(Vec2 centre, Vec2 vertex, int sides, bool inscribed);

} // namespace cad
