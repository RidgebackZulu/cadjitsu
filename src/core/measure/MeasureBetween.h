#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>

namespace cad {

// Where a shape is "centred" for centre-to-centre distances: a point (a
// circle's or sphere's centre, a vertex) or an axis (a cylinder or cone).
struct ShapeCentre {
    enum class Kind { None, Point, Axis } kind = Kind::None;
    gp_Pnt point; // Point, or a point on the Axis
    gp_Ax1 axis;
};
ShapeCentre centreOf(const TopoDS_Shape &s);

// What the Measure tool shows for two picked shapes (vertices, edges, faces,
// solids). Lengths in mm, angles in radians.
struct MeasureResult {
    bool ok = false;
    std::string error;
    // The minimum distance and where it is on each shape.
    double distance = 0.0;
    gp_Pnt p1, p2;
    // Between the centres (circles, arcs, spheres, cylinder axes), when both have one.
    std::optional<double> centreDistance;
    gp_Pnt c1, c2;
    // Between planar faces / straight edges (the smaller angle, 0..90 degrees).
    std::optional<double> angle;
};
MeasureResult measureBetween(const TopoDS_Shape &a, const TopoDS_Shape &b);

// The direction of a planar face's normal or a straight edge, if it has one.
std::optional<gp_Dir> directionOf(const TopoDS_Shape &s, bool *isPlane = nullptr);

} // namespace cad
