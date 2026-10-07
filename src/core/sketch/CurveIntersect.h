#pragma once

#include "sketch/ProfileBuilder.h"

#include <vector>

namespace cad {

struct Sketch;
struct SkEntity;

// Curves are parametrized as follows: lines by t in [0, 1] from p0 to p1;
// arcs by the angle offset from a0 in [0, sweep]; circles by the absolute
// angle in [0, 2 pi).
struct CurveHit {
    double param;
    Vec2 p;
};

// Intersections of two bounded curves within `tol`, appended to `ha` (as
// parameters on A) and `hb` (on B), in matching order (collinear lines add
// the overlap's end points to each side separately).
void intersectCurves(const Curve2 &A, const Curve2 &B, double tol, std::vector<CurveHit> &ha,
                     std::vector<CurveHit> &hb);

// Whether `angle` lies on round curve `cv` (within angTol); its parameter.
bool angleOnCurve(const Curve2 &cv, double angle, double angTol, double &param);

// A sketch curve (line, arc or circle; construction or not) as a Curve2.
Curve2 curveOfEntity(const Sketch &sketch, const SkEntity &e);

// The point at a parameter, the parameter of a point (projected onto the
// curve; for arcs it may fall outside [0, sweep]), and the parameter's end.
Vec2 curvePoint(const Curve2 &c, double param);
double curveParam(const Curve2 &c, Vec2 p);
double curveParamEnd(const Curve2 &c);

} // namespace cad
