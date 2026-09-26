#pragma once

#include <Bnd_Box.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>

#include <string>
#include <vector>

namespace cad {

double volumeOf(const TopoDS_Shape &shape);
double areaOf(const TopoDS_Shape &shape);
double lengthOf(const TopoDS_Shape &edgeOrWire);
gp_Pnt centroidOfFace(const TopoDS_Face &face);
gp_Pnt midpointOfEdge(const TopoDS_Edge &edge);

Bnd_Box boundingBox(const TopoDS_Shape &shape);
double bboxDiagonal(const TopoDS_Shape &shape);

// Solids contained in `shape` (a solid, compsolid or compound).
std::vector<TopoDS_Solid> solidsOf(const TopoDS_Shape &shape);
TopoDS_Shape makeCompound(const std::vector<TopoDS_Shape> &shapes);

// If `face` is planar, returns its plane with the normal pointing out of the
// material (the face orientation is taken into account).
bool planeOfFace(const TopoDS_Face &face, gp_Pln &out);

// Outward unit normal of `face` at the parameter-space point closest to `p`.
bool faceNormalAt(const TopoDS_Face &face, const gp_Pnt &p, gp_Dir &normal);

bool isValidShape(const TopoDS_Shape &shape);

// Stops OCCT from printing translator statistics and traces to stdout.
void quietKernelMessages();

// Human readable surface / curve type ("Plane", "Cylinder", "Line", "Circle"...).
std::string surfaceTypeName(const TopoDS_Face &face);
std::string curveTypeName(const TopoDS_Edge &edge);

// Sketch-frame convention: origin at the projection of the world origin, x axis
// along the projected world X (or Y when the plane is perpendicular to X).
gp_Ax3 frameForPlane(const gp_Pln &plane);

} // namespace cad
