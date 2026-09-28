#include "measure/Measure.h"

#include "geom/OcctUtil.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>

#include <cmath>
#include <cstdio>

namespace cad {

namespace {

std::string number(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s(buf);
    // Thousands separators for readability (volumes get large).
    const size_t dot = s.find('.');
    size_t intEnd = dot == std::string::npos ? s.size() : dot;
    const size_t start = (s[0] == '-') ? 1 : 0;
    for(size_t i = intEnd; i > start + 3; i -= 3) s.insert(i - 3, ",");
    if(s == "-0" || s.rfind("-0.", 0) == 0) {
        bool zero = true;
        for(char c : s)
            if(c >= '1' && c <= '9') zero = false;
        if(zero) s = s.substr(1);
    }
    return s;
}

Measurement len(const std::string &label, double v) { return {label, v, MeasureUnit::Length, {}}; }
Measurement text(const std::string &label, const std::string &t) { return {label, 0.0, MeasureUnit::Text, t}; }

} // namespace

std::string Measurement::format(int decimals) const {
    switch(unit) {
    case MeasureUnit::Length: return label + ": " + number(value, decimals) + " mm";
    case MeasureUnit::Area: return label + ": " + number(value, decimals) + " mm\xC2\xB2";
    case MeasureUnit::Volume: return label + ": " + number(value, decimals) + " mm\xC2\xB3";
    case MeasureUnit::Angle: return label + ": " + number(value * 180.0 / kPi, decimals) + " deg";
    case MeasureUnit::Text: return label.empty() ? text : label + ": " + text;
    }
    return label;
}

std::string formatMeasurements(const std::vector<Measurement> &m, int decimals) {
    std::string out;
    for(size_t i = 0; i < m.size(); ++i) {
        if(i) out += "  |  ";
        out += m[i].format(decimals);
    }
    return out;
}

std::vector<Measurement> measureFace(const TopoDS_Face &face) {
    std::vector<Measurement> out;
    BRepAdaptor_Surface s(face, false);
    out.push_back(text("", surfaceTypeName(face)));
    switch(s.GetType()) {
    case GeomAbs_Cylinder:
        out.push_back(len("Radius", s.Cylinder().Radius()));
        out.push_back(len("Diameter", 2 * s.Cylinder().Radius()));
        break;
    case GeomAbs_Sphere:
        out.push_back(len("Radius", s.Sphere().Radius()));
        break;
    case GeomAbs_Cone:
        out.push_back({"Half angle", std::fabs(s.Cone().SemiAngle()), MeasureUnit::Angle, {}});
        break;
    case GeomAbs_Torus:
        out.push_back(len("Minor radius", s.Torus().MinorRadius()));
        out.push_back(len("Major radius", s.Torus().MajorRadius()));
        break;
    default:
        break;
    }
    out.push_back({"Area", areaOf(face), MeasureUnit::Area, {}});
    return out;
}

std::vector<Measurement> measureEdge(const TopoDS_Edge &edge) {
    std::vector<Measurement> out;
    out.push_back(text("", curveTypeName(edge)));
    if(!BRep_Tool::Degenerated(edge)) {
        BRepAdaptor_Curve c(edge);
        if(c.GetType() == GeomAbs_Circle) {
            out.push_back(len("Radius", c.Circle().Radius()));
            out.push_back(len("Diameter", 2 * c.Circle().Radius()));
        }
    }
    out.push_back(len("Length", lengthOf(edge)));
    return out;
}

std::vector<Measurement> measureVertex(const TopoDS_Vertex &vertex) {
    const gp_Pnt p = BRep_Tool::Pnt(vertex);
    return {len("X", p.X()), len("Y", p.Y()), len("Z", p.Z())};
}

std::vector<Measurement> measureBody(const TopoDS_Shape &solid) {
    std::vector<Measurement> out;
    out.push_back({"Volume", volumeOf(solid), MeasureUnit::Volume, {}});
    out.push_back({"Area", areaOf(solid), MeasureUnit::Area, {}});
    Bnd_Box b = boundingBox(solid);
    if(!b.IsVoid()) {
        double x0, y0, z0, x1, y1, z1;
        b.SetGap(0.0);
        b.Get(x0, y0, z0, x1, y1, z1);
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s x %s x %s mm", number(x1 - x0, 2).c_str(), number(y1 - y0, 2).c_str(),
                      number(z1 - z0, 2).c_str());
        out.push_back(text("Size", buf));
    }
    return out;
}

std::vector<Measurement> measureSketchEntity(const Sketch &sk, int entityId) {
    std::vector<Measurement> out;
    const SkEntity *e = sk.find(entityId);
    if(!e) return out;
    switch(e->type) {
    case SkType::Point:
        out.push_back(len("X", e->x));
        out.push_back(len("Y", e->y));
        break;
    case SkType::Line: {
        const Vec2 a = sk.pointPos(e->a), b = sk.pointPos(e->b);
        out.push_back(len("Length", distance(a, b)));
        double ang = std::atan2(b.y - a.y, b.x - a.x);
        if(ang < 0) ang += kPi;
        out.push_back({"Angle", ang, MeasureUnit::Angle, {}});
        break;
    }
    case SkType::Circle:
        out.push_back(len("Radius", e->r));
        out.push_back(len("Diameter", 2 * e->r));
        out.push_back(len("Circumference", 2 * kPi * e->r));
        break;
    case SkType::Arc: {
        const Vec2 c = sk.pointPos(e->a), s = sk.pointPos(e->b), t = sk.pointPos(e->c);
        const double r = distance(c, s);
        double sweep = normAngle((t - c).angle() - (s - c).angle());
        if(sweep < 1e-12) sweep = 2 * kPi;
        out.push_back(len("Radius", r));
        out.push_back(len("Diameter", 2 * r));
        out.push_back(len("Arc length", r * sweep));
        out.push_back({"Sweep", sweep, MeasureUnit::Angle, {}});
        break;
    }
    case SkType::Text:
        out.push_back({"Text", 0.0, MeasureUnit::Text, e->text});
        out.push_back(len("Size", e->size));
        break;
    }
    return out;
}

} // namespace cad
