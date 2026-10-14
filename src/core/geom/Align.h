#pragma once

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <optional>
#include <string>

namespace cad {

// Align (Fusion's): geometry picked on what moves (from) is brought onto
// geometry picked elsewhere (to). Each pick is reduced to an anchor:
//   Point - a vertex, a circle's centre, a sketch point;
//   Line  - a straight edge or line (`p` the end picked, or its middle;
//           `bounded` false for an axis, whose `p` is just a point on it), or
//           the axis of a circle or cylinder (`p` its centre);
//   Plane - a flat face or plane (`p` its centre, `d` its outward normal).
enum class AlignKind { Point, Line, Plane };

struct AlignAnchor {
    AlignKind kind = AlignKind::Point;
    gp_Pnt p;
    gp_Dir d = gp_Dir(0, 0, 1);
    bool bounded = true; // a line with ends (an edge), not an endless axis
};

// What a pair does:
//   anything -> point: moved so its point (end, centre) sits on the point;
//   point -> line or plane: slid to the nearest point on it;
//   line -> line: turned along the target (`flip` the other way), its point
//       onto the target's (an axis: the nearest point on it), and then
//       turned `angle` (radians) about the shared line;
//   plane -> plane: turned to face the target (flush; `flip`: the same way),
//       centre onto centre, then turned `angle` about the shared normal;
//   line -> plane: laid in the plane (along its own direction there), then
//       turned `angle` about the plane's normal.
// A face onto a line has no single answer: nullopt with `why`.
// `upright` settles a half turn (from exactly opposite to exactly along): the
// turn is about it when it is square to the direction (sketches pass their
// normal, so a half turn stays in the sketch plane).
std::optional<gp_Trsf> alignTransform(const AlignAnchor &from, const AlignAnchor &to, bool flip, double angle,
                                      std::string &why, std::optional<gp_Dir> upright = std::nullopt);

// What a pair does, in a sentence for the dialog ("the faces meet flat...").
std::string describeAlign(AlignKind from, AlignKind to);
// "point", "line", "face".
const char *alignKindName(AlignKind k);

} // namespace cad
