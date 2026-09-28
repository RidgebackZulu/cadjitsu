#pragma once

#include "base/Json.h"
#include "base/Vec2.h"

#include <string>
#include <vector>

namespace cad {

// ---------------------------------------------------------------------------
// Sketch geometry. Coordinates are 2D in the sketch plane, in millimetres.
// Curves reference point entities, so a shared endpoint is structurally
// coincident (a rectangle is 4 points + 4 lines).

enum class SkType { Point, Line, Circle, Arc, Text };

// Fixed reference geometry every sketch has; constraints may use these ids.
constexpr int kSketchOrigin = -1; // the sketch origin point (0, 0)
constexpr int kSketchXAxis = -2;  // the sketch X axis line
constexpr int kSketchYAxis = -3;  // the sketch Y axis line

struct SkEntity {
    int id = 0;
    SkType type = SkType::Point;
    bool construction = false;

    // Point
    double x = 0.0, y = 0.0;
    // Line: a -> b. Circle: a = centre. Arc: a = centre, b = start, c = end,
    // running counter-clockwise from start to end.
    int a = 0, b = 0, c = 0;
    // Circle radius (an arc's radius is |start - centre|).
    double r = 0.0;
    // Text: a = its origin (the first line's baseline start). Its letters are
    // regions of the sketch (they can be extruded) but not solver geometry.
    std::string text;
    std::string font = "DejaVu Sans";
    bool bold = false, italic = false;
    bool mirror = false;   // flipped left-right (reads from the other side)
    double size = 5.0;     // the font's em size (mm)
    double angle = 0.0;    // degrees, anticlockwise

    bool isCurve() const { return type == SkType::Line || type == SkType::Circle || type == SkType::Arc; }
    bool isText() const { return type == SkType::Text; }
};

enum class SkCon {
    // Geometric
    Coincident,     // e1 point, e2 point
    PointOnCurve,   // e1 point, e2 line/circle/arc
    Horizontal,     // e1 line  (or e1, e2 points)
    Vertical,       // e1 line  (or e1, e2 points)
    Parallel,       // e1 line, e2 line
    Perpendicular,  // e1 line, e2 line
    Tangent,        // e1 curve, e2 curve
    Equal,          // e1, e2: lines (length) or circles/arcs (radius)
    Midpoint,       // e1 point, e2 line
    Concentric,     // e1, e2 circles/arcs
    Symmetric,      // e1 point, e2 point, e3 line
    Fix,            // e1 point
    // Dimensional (value from `param`)
    Distance,       // e1 point, e2 point (aligned)   or e1 line (length)
    HDistance,      // e1 point, e2 point (along sketch X)
    VDistance,      // e1 point, e2 point (along sketch Y)
    PointLineDistance, // e1 point, e2 line
    Radius,         // e1 circle/arc
    Diameter,       // e1 circle/arc
    Angle,          // e1 line, e2 line
    OffsetRadius,   // e1 circle/arc, e2 circle/arc: e1's radius is e2's plus or minus the value
                    // (on the side it is on); the Offset tool pairs it with Concentric
};

bool isDimension(SkCon t);
const char *toString(SkCon t);
bool skConFromString(const std::string &s, SkCon &out);

struct SkConstraint {
    int id = 0;
    SkCon type = SkCon::Coincident;
    int e1 = 0, e2 = 0, e3 = 0;
    std::string param;     // dimensions: name of the model parameter holding the value
    std::string expr;      // dimensions: the parameter's expression, e.g. "20 mm" or "d1 * 2"
    bool driven = false;   // reference ("driven") dimension: displays, does not constrain
    bool supplementary = false; // Angle: measure the supplementary angle
    int valueFrom = 0;     // dimensions: take the value of dimension `valueFrom` instead of a
                           // parameter of its own, and are not drawn (the Offset tool's
                           // curves all follow its one offset distance)
    Vec2 label;            // dimension label offset from the dimension's anchor (sketch units):
                           // the midpoint of the measured points, a circle's centre, or the
                           // intersection of an angle's lines; labels follow their geometry
};

struct Sketch {
    std::vector<SkEntity> entities;
    std::vector<SkConstraint> constraints;
    int nextId = 1;

    SkEntity *find(int id);
    const SkEntity *find(int id) const;
    const SkConstraint *findConstraint(int id) const;
    SkConstraint *findConstraint(int id);

    Vec2 pointPos(int pointId) const;

    // Builders; return the new entity id.
    int addPoint(double x, double y, bool construction = false);
    int addLine(int p1, int p2, bool construction = false);
    int addLine(Vec2 a, Vec2 b, bool construction = false);
    int addCircle(int centre, double radius, bool construction = false);
    int addCircle(Vec2 centre, double radius, bool construction = false);
    int addArc(int centre, int start, int end, bool construction = false);
    // Text at `origin` (a point entity); style fields are set on the result.
    int addText(int origin, const std::string &text, bool construction = false);
    int addText(Vec2 origin, const std::string &text, bool construction = false);
    // Axis-aligned rectangle from two corners: 4 points, 4 lines, H/V constraints.
    // Returns the line ids in order bottom, right, top, left.
    std::vector<int> addRectangle(Vec2 corner1, Vec2 corner2, bool construction = false);
    int addConstraint(SkCon type, int e1, int e2 = 0, int e3 = 0, const std::string &param = {});

    // Removes an entity, constraints that reference it, and points no longer used.
    void removeEntity(int id);
    // Removes a constraint, and the dimensions that take their value from it.
    void removeConstraint(int id);

    // Drops dimensions whose `valueFrom` constraint no longer exists.
    void dropOrphans();

    double arcRadius(const SkEntity &arc) const;

    json toJson() const;
    static Sketch fromJson(const json &j);
};

} // namespace cad
