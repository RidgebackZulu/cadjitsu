#include "geom/OcctUtil.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>

#include <cmath>
#include <mutex>

namespace cad {

double volumeOf(const TopoDS_Shape &shape) {
    if(shape.IsNull()) return 0.0;
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

double areaOf(const TopoDS_Shape &shape) {
    if(shape.IsNull()) return 0.0;
    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape, props);
    return props.Mass();
}

double lengthOf(const TopoDS_Shape &shape) {
    if(shape.IsNull()) return 0.0;
    GProp_GProps props;
    BRepGProp::LinearProperties(shape, props);
    return props.Mass();
}

gp_Pnt centroidOfFace(const TopoDS_Face &face) {
    GProp_GProps props;
    BRepGProp::SurfaceProperties(face, props);
    if(props.Mass() <= 0.0) {
        Bnd_Box b = boundingBox(face);
        if(b.IsVoid()) return gp_Pnt();
        double x0, y0, z0, x1, y1, z1;
        b.Get(x0, y0, z0, x1, y1, z1);
        return gp_Pnt(0.5 * (x0 + x1), 0.5 * (y0 + y1), 0.5 * (z0 + z1));
    }
    return props.CentreOfMass();
}

gp_Pnt midpointOfEdge(const TopoDS_Edge &edge) {
    if(BRep_Tool::Degenerated(edge)) {
        TopExp_Explorer ex(edge, TopAbs_VERTEX);
        return ex.More() ? BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())) : gp_Pnt();
    }
    BRepAdaptor_Curve c(edge);
    return c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
}

Bnd_Box boundingBox(const TopoDS_Shape &shape) {
    Bnd_Box box;
    if(!shape.IsNull()) BRepBndLib::Add(shape, box, false);
    return box;
}

double bboxDiagonal(const TopoDS_Shape &shape) {
    Bnd_Box b = boundingBox(shape);
    if(b.IsVoid()) return 0.0;
    return std::sqrt(b.SquareExtent());
}

std::vector<TopoDS_Solid> solidsOf(const TopoDS_Shape &shape) {
    std::vector<TopoDS_Solid> out;
    if(shape.IsNull()) return out;
    for(TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) out.push_back(TopoDS::Solid(ex.Current()));
    return out;
}

TopoDS_Shape makeCompound(const std::vector<TopoDS_Shape> &shapes) {
    BRep_Builder b;
    TopoDS_Compound c;
    b.MakeCompound(c);
    for(const auto &s : shapes)
        if(!s.IsNull()) b.Add(c, s);
    return c;
}

bool planeOfFace(const TopoDS_Face &face, gp_Pln &out) {
    BRepAdaptor_Surface s(face, false);
    if(s.GetType() != GeomAbs_Plane) return false;
    gp_Pln pln = s.Plane();
    if(face.Orientation() == TopAbs_REVERSED) {
        gp_Ax3 ax = pln.Position();
        ax.ZReverse();
        pln = gp_Pln(ax);
    }
    out = pln;
    return true;
}

bool faceNormalAt(const TopoDS_Face &face, const gp_Pnt &p, gp_Dir &normal) {
    BRepAdaptor_Surface s(face);
    GeomAPI_ProjectPointOnSurf proj(p, BRep_Tool::Surface(face));
    if(proj.NbPoints() == 0) return false;
    double u, v;
    proj.LowerDistanceParameters(u, v);
    BRepLProp_SLProps props(s, u, v, 1, 1e-7);
    if(!props.IsNormalDefined()) return false;
    normal = props.Normal();
    if(face.Orientation() == TopAbs_REVERSED) normal.Reverse();
    return true;
}

void quietKernelMessages() {
    static std::once_flag once;
    std::call_once(once, [] {
        const Handle(Message_Messenger) &m = Message::DefaultMessenger();
        for(Message_SequenceOfPrinters::Iterator it(m->Printers()); it.More(); it.Next())
            it.Value()->SetTraceLevel(Message_Fail);
    });
}

bool isValidShape(const TopoDS_Shape &shape) {
    if(shape.IsNull()) return false;
    BRepCheck_Analyzer an(shape);
    return an.IsValid();
}

std::string surfaceTypeName(const TopoDS_Face &face) {
    BRepAdaptor_Surface s(face, false);
    switch(s.GetType()) {
    case GeomAbs_Plane: return "Plane";
    case GeomAbs_Cylinder: return "Cylinder";
    case GeomAbs_Cone: return "Cone";
    case GeomAbs_Sphere: return "Sphere";
    case GeomAbs_Torus: return "Torus";
    case GeomAbs_BezierSurface: return "Bezier surface";
    case GeomAbs_BSplineSurface: return "B-spline surface";
    case GeomAbs_SurfaceOfRevolution: return "Surface of revolution";
    case GeomAbs_SurfaceOfExtrusion: return "Surface of extrusion";
    case GeomAbs_OffsetSurface: return "Offset surface";
    default: return "Surface";
    }
}

std::string curveTypeName(const TopoDS_Edge &edge) {
    if(BRep_Tool::Degenerated(edge)) return "Degenerate";
    BRepAdaptor_Curve c(edge);
    switch(c.GetType()) {
    case GeomAbs_Line: return "Line";
    case GeomAbs_Circle: return "Circle";
    case GeomAbs_Ellipse: return "Ellipse";
    case GeomAbs_Hyperbola: return "Hyperbola";
    case GeomAbs_Parabola: return "Parabola";
    case GeomAbs_BezierCurve: return "Bezier curve";
    case GeomAbs_BSplineCurve: return "B-spline curve";
    default: return "Curve";
    }
}

gp_Ax3 frameForPlane(const gp_Pln &plane) {
    const gp_Dir n = plane.Axis().Direction();
    const gp_Pnt o = plane.Location();
    // Project the world origin onto the plane.
    const gp_Vec ov(o.XYZ());
    const double d = ov.Dot(gp_Vec(n));
    const gp_Pnt origin(n.XYZ() * d);
    gp_Vec x(1, 0, 0);
    if(std::fabs(n.X()) > 0.999) x = gp_Vec(0, 1, 0);
    x -= gp_Vec(n) * x.Dot(gp_Vec(n));
    return gp_Ax3(origin, n, gp_Dir(x));
}

} // namespace cad
