#include "measure/MeasureBetween.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Tool.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <gp_Lin.hxx>

#include <cmath>

namespace cad {

namespace {

constexpr double kPiLocal = 3.14159265358979323846;

// Folds an angle between directions to 0..90 degrees.
double acuteAngle(double a) {
    a = std::fabs(std::fmod(a, kPiLocal));
    return a > kPiLocal / 2 ? kPiLocal - a : a;
}

gp_Pnt project(const gp_Pnt &p, const gp_Ax1 &axis) {
    const gp_Vec d(axis.Direction());
    return axis.Location().Translated(d * gp_Vec(axis.Location(), p).Dot(d));
}

} // namespace

ShapeCentre centreOf(const TopoDS_Shape &s) {
    ShapeCentre c;
    if(s.IsNull()) return c;
    switch(s.ShapeType()) {
    case TopAbs_VERTEX:
        c.kind = ShapeCentre::Kind::Point;
        c.point = BRep_Tool::Pnt(TopoDS::Vertex(s));
        break;
    case TopAbs_EDGE: {
        BRepAdaptor_Curve a(TopoDS::Edge(s));
        if(a.GetType() == GeomAbs_Circle) {
            c.kind = ShapeCentre::Kind::Point;
            c.point = a.Circle().Location();
            c.axis = a.Circle().Axis();
        }
        break;
    }
    case TopAbs_FACE: {
        BRepAdaptor_Surface a(TopoDS::Face(s));
        switch(a.GetType()) {
        case GeomAbs_Cylinder:
            c.kind = ShapeCentre::Kind::Axis;
            c.axis = a.Cylinder().Axis();
            c.point = c.axis.Location();
            break;
        case GeomAbs_Cone:
            c.kind = ShapeCentre::Kind::Axis;
            c.axis = a.Cone().Axis();
            c.point = c.axis.Location();
            break;
        case GeomAbs_Sphere:
            c.kind = ShapeCentre::Kind::Point;
            c.point = a.Sphere().Location();
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    return c;
}

std::optional<gp_Dir> directionOf(const TopoDS_Shape &s, bool *isPlane) {
    if(s.IsNull()) return std::nullopt;
    if(s.ShapeType() == TopAbs_FACE) {
        BRepAdaptor_Surface a(TopoDS::Face(s));
        if(a.GetType() != GeomAbs_Plane) return std::nullopt;
        if(isPlane) *isPlane = true;
        return a.Plane().Axis().Direction();
    }
    if(s.ShapeType() == TopAbs_EDGE) {
        BRepAdaptor_Curve a(TopoDS::Edge(s));
        if(a.GetType() != GeomAbs_Line) return std::nullopt;
        if(isPlane) *isPlane = false;
        return a.Line().Direction();
    }
    return std::nullopt;
}

MeasureResult measureBetween(const TopoDS_Shape &a, const TopoDS_Shape &b) {
    MeasureResult r;
    if(a.IsNull() || b.IsNull()) {
        r.error = "nothing to measure";
        return r;
    }
    try {
        BRepExtrema_DistShapeShape d(a, b);
        if(!d.IsDone() || d.NbSolution() < 1) {
            r.error = "the distance could not be worked out";
            return r;
        }
        r.ok = true;
        r.distance = d.Value();
        r.p1 = d.PointOnShape1(1);
        r.p2 = d.PointOnShape2(1);
    } catch(const Standard_Failure &e) {
        r.error = e.GetMessageString() ? e.GetMessageString() : "the distance could not be worked out";
        return r;
    }

    // Centre to centre.
    const ShapeCentre ca = centreOf(a), cb = centreOf(b);
    using K = ShapeCentre::Kind;
    if(ca.kind == K::Point && cb.kind == K::Point) {
        r.c1 = ca.point;
        r.c2 = cb.point;
        r.centreDistance = ca.point.Distance(cb.point);
    } else if(ca.kind == K::Point && cb.kind == K::Axis) {
        r.c1 = ca.point;
        r.c2 = project(ca.point, cb.axis);
        r.centreDistance = r.c1.Distance(r.c2);
    } else if(ca.kind == K::Axis && cb.kind == K::Point) {
        r.c2 = cb.point;
        r.c1 = project(cb.point, ca.axis);
        r.centreDistance = r.c1.Distance(r.c2);
    } else if(ca.kind == K::Axis && cb.kind == K::Axis && ca.axis.IsParallel(cb.axis, 1e-9)) {
        // Parallel axes: measured square to both, level with the middle of the first contact.
        r.c1 = project(r.p1, ca.axis);
        r.c2 = project(r.c1, cb.axis);
        r.centreDistance = r.c1.Distance(r.c2);
    }

    // The angle between planes / straight edges.
    bool planeA = false, planeB = false;
    const std::optional<gp_Dir> da = directionOf(a, &planeA), db = directionOf(b, &planeB);
    if(da && db) {
        const double between = da->Angle(*db);
        // A line against a plane: measured from the plane, not its normal.
        r.angle = planeA != planeB ? kPiLocal / 2 - acuteAngle(between) : acuteAngle(between);
    }
    return r;
}

} // namespace cad
