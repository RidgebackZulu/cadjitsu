#include "features/TextFeature.h"

#include "base/KernelLock.h"
#include "features/BodyOps.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <ElCLib.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomConvert.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <ShapeFix_Face.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>

#include <algorithm>
#include <cmath>

namespace cad {

const char *toString(TextDirection d) { return d == TextDirection::Emboss ? "emboss" : "engrave"; }

TextDirection textDirectionFromString(const std::string &s) {
    return s == "emboss" ? TextDirection::Emboss : TextDirection::Engrave;
}

// --- the frame on a face ---------------------------------------------------------------

namespace {

// A readable frame on a plane: y up on upright faces (x to the right seen from
// outside), x along world X on flat ones; the origin is the world origin
// projected onto the plane.
gp_Ax3 textFrame(const gp_Pln &pln) {
    const gp_Dir n = pln.Axis().Direction();
    const gp_Pnt origin(n.XYZ() * gp_Vec(pln.Location().XYZ()).Dot(gp_Vec(n)));
    gp_Vec x;
    if(std::fabs(n.Z()) < 0.9) {
        gp_Vec up(0, 0, 1);
        up -= gp_Vec(n) * up.Dot(gp_Vec(n));
        x = up.Crossed(gp_Vec(n));
    } else {
        x = gp_Vec(1, 0, 0);
        x -= gp_Vec(n) * x.Dot(gp_Vec(n));
    }
    return gp_Ax3(origin, n, gp_Dir(x));
}

} // namespace

gp_Pnt2d FaceTextFrame::uv(double x, double y) const {
    if(planar) return gp_Pnt2d(x, y);
    return gp_Pnt2d(u0 + sign * flip * x / su, v0 + flip * y / sv);
}

gp_Pnt FaceTextFrame::point(double x, double y, double offset) const {
    const gp_Pnt2d p = uv(x, y);
    gp_Pnt at = surface->Value(p.X(), p.Y());
    if(offset != 0.0) at.Translate(gp_Vec(normal(x, y)) * offset);
    return at;
}

gp_Dir FaceTextFrame::normal(double x, double y) const {
    if(planar) return plane.Direction();
    const gp_Pnt2d p = uv(x, y);
    GeomLProp_SLProps props(surface, p.X(), p.Y(), 1, 1e-9);
    if(!props.IsNormalDefined()) return plane.Direction();
    gp_Dir n = props.Normal();
    return sign < 0 ? n.Reversed() : n;
}

void FaceTextFrame::axes(double x, double y, gp_Dir &xDir, gp_Dir &yDir) const {
    if(planar) {
        xDir = plane.XDirection();
        yDir = plane.YDirection();
        return;
    }
    const gp_Pnt2d p = uv(x, y);
    gp_Pnt at;
    gp_Vec du, dv;
    surface->D1(p.X(), p.Y(), at, du, dv);
    if(du.Magnitude() < 1e-12 || dv.Magnitude() < 1e-12) {
        xDir = plane.XDirection();
        yDir = plane.YDirection();
        return;
    }
    xDir = gp_Dir(du * (sign * flip));
    yDir = gp_Dir(dv * flip);
}

bool FaceTextFrame::locate(const gp_Pnt &p, double &x, double &y) const {
    if(planar) {
        const gp_Vec d(plane.Location(), p);
        x = d.Dot(gp_Vec(plane.XDirection()));
        y = d.Dot(gp_Vec(plane.YDirection()));
        return true;
    }
    GeomAPI_ProjectPointOnSurf proj(p, surface);
    if(!proj.IsDone() || proj.NbPoints() == 0) return false;
    double u, v;
    proj.LowerDistanceParameters(u, v);
    if(surface->IsUPeriodic()) u = ElCLib::InPeriod(u, u0 - surface->UPeriod() / 2, u0 + surface->UPeriod() / 2);
    if(surface->IsVPeriodic()) v = ElCLib::InPeriod(v, v0 - surface->VPeriod() / 2, v0 + surface->VPeriod() / 2);
    x = sign * flip * (u - u0) * su;
    y = flip * (v - v0) * sv;
    return true;
}

bool faceTextFrame(const TopoDS_Face &face, FaceTextFrame &out, std::string &error) {
    out = FaceTextFrame();
    gp_Pln pln;
    if(planeOfFace(face, pln)) {
        out.planar = true;
        out.plane = textFrame(pln);
        out.surface = new Geom_Plane(out.plane);
        return true;
    }
    out.surface = BRep_Tool::Surface(face);
    if(out.surface.IsNull()) {
        error = "the face has no surface";
        return false;
    }
    double u1, u2, v1, v2;
    BRepTools::UVBounds(face, u1, u2, v1, v2);
    out.u0 = 0.5 * (u1 + u2);
    out.v0 = 0.5 * (v1 + v2);
    gp_Pnt at;
    gp_Vec du, dv;
    out.surface->D1(out.u0, out.v0, at, du, dv);
    out.su = du.Magnitude();
    out.sv = dv.Magnitude();
    if(out.su < 1e-9 || out.sv < 1e-9) {
        error = "text cannot be placed on this face (its surface is degenerate at its middle)";
        return false;
    }
    out.sign = face.Orientation() == TopAbs_REVERSED ? -1.0 : 1.0;
    // Upright where the surface allows: y (along v) towards world +Z.
    if(dv.Z() / out.sv < -1e-6) out.flip = -1.0;
    // A fallback plane frame (normal and directions) at the middle.
    gp_Dir n(du.Crossed(dv));
    if(out.sign < 0) n.Reverse();
    out.plane = gp_Ax3(at, n, gp_Dir(du * (out.sign * out.flip)));
    return true;
}

// --- letters on the face ---------------------------------------------------------------

namespace {

// Text coordinates (the glyph layout) to surface parameters: uv = A t + b.
struct Affine {
    double a11 = 1, a12 = 0, a21 = 0, a22 = 1, b1 = 0, b2 = 0;
    gp_Pnt2d operator()(double x, double y) const { return {a11 * x + a12 * y + b1, a21 * x + a22 * y + b2}; }
};

// Where a text-layout point goes in the face frame: turned about the text's
// middle, which is put at (in.x, in.y).
struct Layout {
    double cx = 0, cy = 0, c = 1, s = 0, px = 0, py = 0;
    Layout(const TextShape &shape, const TextPlacementInput &in) {
        cx = 0.5 * (shape.min.x + shape.max.x);
        cy = 0.5 * (shape.min.y + shape.max.y);
        c = std::cos(in.rotation);
        s = std::sin(in.rotation);
        px = in.x;
        py = in.y;
    }
    void operator()(double tx, double ty, double &x, double &y) const {
        const double dx = tx - cx, dy = ty - cy;
        x = px + c * dx - s * dy;
        y = py + s * dx + c * dy;
    }
};

Affine uvMap(const FaceTextFrame &frame, const Layout &l) {
    // xy = R (t - c) + p; uv = D xy + e.
    const double d1 = frame.planar ? 1.0 : frame.sign * frame.flip / frame.su,
                 d2 = frame.planar ? 1.0 : frame.flip / frame.sv;
    const double e1 = frame.planar ? 0.0 : frame.u0, e2 = frame.planar ? 0.0 : frame.v0;
    Affine m;
    m.a11 = d1 * l.c;
    m.a12 = -d1 * l.s;
    m.a21 = d2 * l.s;
    m.a22 = d2 * l.c;
    const double qx = l.px - (l.c * l.cx - l.s * l.cy), qy = l.py - (l.s * l.cx + l.c * l.cy);
    m.b1 = d1 * qx + e1;
    m.b2 = d2 * qy + e2;
    return m;
}

// A glyph edge (a curve in the layout's XY plane) as a curve in surface parameters.
Handle(Geom2d_BSplineCurve) mapEdge(const TopoDS_Edge &e, const Affine &m) {
    TopLoc_Location loc;
    double f, l;
    Handle(Geom_Curve) c = BRep_Tool::Curve(e, loc, f, l);
    if(c.IsNull()) return nullptr;
    Handle(Geom_Curve) trimmed = new Geom_TrimmedCurve(c, f, l);
    if(!loc.IsIdentity()) trimmed = Handle(Geom_Curve)::DownCast(trimmed->Transformed(loc.Transformation()));
    Handle(Geom_BSplineCurve) bs = GeomConvert::CurveToBSplineCurve(trimmed);
    if(bs.IsNull()) return nullptr;
    TColgp_Array1OfPnt2d poles(1, bs->NbPoles());
    for(int i = 1; i <= bs->NbPoles(); ++i) {
        const gp_Pnt p = bs->Pole(i);
        poles(i) = m(p.X(), p.Y());
    }
    TColStd_Array1OfReal knots(1, bs->NbKnots());
    TColStd_Array1OfInteger mults(1, bs->NbKnots());
    bs->Knots(knots);
    bs->Multiplicities(mults);
    if(bs->IsRational()) {
        TColStd_Array1OfReal weights(1, bs->NbPoles());
        bs->Weights(weights);
        return new Geom2d_BSplineCurve(poles, weights, knots, mults, bs->Degree(), bs->IsPeriodic());
    }
    return new Geom2d_BSplineCurve(poles, knots, mults, bs->Degree(), bs->IsPeriodic());
}

// A glyph face rebuilt on `surface` through the parameter map. Each loop is
// turned to run anticlockwise in (u, v) (holes clockwise), whatever the map
// does to it (a reversed face flips u).
bool mapFace(const TopoDS_Face &glyph, const Affine &m, const Handle(Geom_Surface) &surface, TopoDS_Face &out,
             std::string &error) {
    const TopoDS_Wire outer = BRepTools::OuterWire(glyph);
    std::vector<TopoDS_Wire> wires;
    for(TopExp_Explorer wx(glyph, TopAbs_WIRE); wx.More(); wx.Next()) {
        const TopoDS_Wire w = TopoDS::Wire(wx.Current());
        BRepBuilderAPI_MakeWire mw;
        double area = 0.0;
        for(BRepTools_WireExplorer ex(w, glyph); ex.More(); ex.Next()) {
            Handle(Geom2d_BSplineCurve) c = mapEdge(ex.Current(), m);
            if(c.IsNull()) continue;
            BRepBuilderAPI_MakeEdge me(c, surface, c->FirstParameter(), c->LastParameter());
            if(!me.IsDone()) {
                error = "a letter outline could not be put on the face";
                return false;
            }
            TopoDS_Edge edge = me.Edge();
            const bool rev = ex.Current().Orientation() == TopAbs_REVERSED;
            if(rev) edge.Reverse();
            // Shoelace over points along the curve, in the loop's direction.
            const int n = 12;
            const double f = c->FirstParameter(), l = c->LastParameter();
            gp_Pnt2d prev = c->Value(rev ? l : f);
            for(int k = 1; k <= n; ++k) {
                const double t = rev ? l - (l - f) * k / n : f + (l - f) * k / n;
                const gp_Pnt2d p = c->Value(t);
                area += prev.X() * p.Y() - p.X() * prev.Y();
                prev = p;
            }
            mw.Add(edge);
            if(!mw.IsDone()) {
                error = "a letter outline does not close up on the face";
                return false;
            }
        }
        if(!mw.IsDone()) continue;
        const bool isOuter = w.IsSame(outer);
        TopoDS_Wire made = mw.Wire();
        if((isOuter && area < 0) || (!isOuter && area > 0)) made.Reverse();
        if(isOuter) wires.insert(wires.begin(), made);
        else wires.push_back(made);
    }
    if(wires.empty()) {
        error = "a letter has no outline";
        return false;
    }
    BRepBuilderAPI_MakeFace mf(surface, wires.front(), Standard_True);
    for(size_t i = 1; i < wires.size() && mf.IsDone(); ++i) mf.Add(wires[i]);
    if(!mf.IsDone()) {
        error = "a letter could not be put on the face";
        return false;
    }
    TopoDS_Face face = mf.Face();
    BRepLib::BuildCurves3d(face);
    ShapeFix_Face fix(face);
    fix.FixOrientationMode() = 0;
    fix.Perform();
    out = fix.Face();
    return !out.IsNull();
}

} // namespace

bool textSolids(const FaceTextFrame &frame, const TextShape &shape, const TextPlacementInput &in, double below,
                double above, std::vector<TopoDS_Shape> &solids, std::string &error) {
    solids.clear();
    const Layout layout(shape, in);
    const Affine m = uvMap(frame, layout);
    // The letters' bottoms: on a copy of the surface `below` under it.
    Handle(Geom_Surface) base;
    if(frame.planar) {
        gp_Ax3 ax = frame.plane;
        ax.Translate(gp_Vec(frame.plane.Direction()) * -below);
        base = new Geom_Plane(ax);
    } else {
        base = new Geom_OffsetSurface(frame.surface, -below * frame.sign);
    }
    for(TopExp_Explorer fx(shape.faces, TopAbs_FACE); fx.More(); fx.Next()) {
        TopoDS_Face letter;
        if(!mapFace(TopoDS::Face(fx.Current()), m, base, letter, error)) return false;
        TopoDS_Shape solid;
        if(frame.planar) {
            BRepPrimAPI_MakePrism prism(letter, gp_Vec(frame.plane.Direction()) * (below + above));
            if(!prism.IsDone()) {
                error = "a letter could not be made solid";
                return false;
            }
            solid = prism.Shape();
        } else {
            BRepOffsetAPI_MakeThickSolid thick;
            const double t = (below + above) * frame.sign * (letter.Orientation() == TopAbs_REVERSED ? -1.0 : 1.0);
            thick.MakeThickSolidBySimple(letter, t);
            thick.Build();
            if(!thick.IsDone()) {
                error = "a letter could not be made solid on this curved face";
                return false;
            }
            solid = thick.Shape();
        }
        solids.push_back(solid);
    }
    if(solids.empty()) {
        error = "the text has no letters";
        return false;
    }
    return true;
}

TextOnFace layTextOnFace(const TopoDS_Face &face, const TextPlacementInput &in) {
    TextOnFace out;
    if(!faceTextFrame(face, out.frame, out.error)) return out;
    const auto shape = buildText(in.text, in.style);
    if(!shape->ok) {
        out.error = shape->error;
        return out;
    }
    out.warning = shape->warning;
    out.width = shape->max.x - shape->min.x;
    out.height = shape->max.y - shape->min.y;
    const Layout layout(*shape, in);
    KernelLock lock(kernelMutex());
    bool outside = false;
    for(const auto &loop : shape->loops) {
        std::vector<gp_Pnt> pts;
        std::vector<std::pair<double, double>> xy;
        pts.reserve(loop.size());
        xy.reserve(loop.size());
        for(size_t i = 0; i < loop.size(); ++i) {
            double x, y;
            layout(loop[i].x, loop[i].y, x, y);
            xy.push_back({x, y});
            pts.push_back(out.frame.point(x, y));
            if(!outside) {
                BRepClass_FaceClassifier cls(face, pts.back(), 1e-3);
                outside = cls.State() == TopAbs_OUT;
            }
        }
        out.outlines.push_back(std::move(pts));
        out.outlinesXY.push_back(std::move(xy));
    }
    if(outside && out.warning.empty()) out.warning = "the text runs past the edge of the face";
    out.ok = true;
    return out;
}

// --- the feature -----------------------------------------------------------------------

std::vector<ParamDef> TextFeature::params() const {
    std::vector<ParamDef> out;
    auto add = [&](const ParamSlot &s, ValueKind k, const char *label) {
        if(!s.empty()) out.push_back({s.name, s.expr, k, id, name + " " + label});
    };
    add(size, ValueKind::Length, "Size");
    add(letterSpacing, ValueKind::Length, "Letter Spacing");
    add(lineSpacing, ValueKind::Scalar, "Line Spacing");
    add(x, ValueKind::Length, "X");
    add(y, ValueKind::Length, "Y");
    add(rotation, ValueKind::Angle, "Rotation");
    add(depth, ValueKind::Length, "Depth");
    return out;
}

json TextFeature::dataToJson() const {
    json j{{"text", text},
           {"font", font},
           {"bold", bold},
           {"italic", italic},
           {"mirror", mirror},
           {"size", size.toJson()},
           {"letterSpacing", letterSpacing.toJson()},
           {"lineSpacing", lineSpacing.toJson()},
           {"x", x.toJson()},
           {"y", y.toJson()},
           {"rotation", rotation.toJson()},
           {"depth", depth.toJson()},
           {"direction", toString(direction)}};
    if(!face.empty()) j["face"] = face.toJson();
    return j;
}

void TextFeature::dataFromJson(const json &j) {
    face = TopoRef::fromJson(j.value("face", json()));
    text = jget<std::string>(j, "text", "");
    font = jget<std::string>(j, "font", "DejaVu Sans");
    bold = jget<bool>(j, "bold", false);
    italic = jget<bool>(j, "italic", false);
    mirror = jget<bool>(j, "mirror", false);
    size = ParamSlot::fromJson(j.value("size", json()));
    letterSpacing = ParamSlot::fromJson(j.value("letterSpacing", json()));
    lineSpacing = ParamSlot::fromJson(j.value("lineSpacing", json()));
    x = ParamSlot::fromJson(j.value("x", json()));
    y = ParamSlot::fromJson(j.value("y", json()));
    rotation = ParamSlot::fromJson(j.value("rotation", json()));
    depth = ParamSlot::fromJson(j.value("depth", json()));
    direction = textDirectionFromString(jget<std::string>(j, "direction", "engrave"));
}

FeatureResult TextFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        if(face.empty()) return {input, Status::error("pick the face the text goes on")};
        if(text.find_first_not_of(" \t\r\n") == std::string::npos) return {input, Status::error("type some text")};
        ResolvedRef r = resolveRef(*input, face);
        st.merge(r.status);
        if(!r.ok) return {input, st};
        if(r.shape.ShapeType() != TopAbs_FACE) return {input, Status::error("text goes on a face")};
        const TopoDS_Face target = TopoDS::Face(r.shape);

        TextPlacementInput in;
        in.text = text;
        in.style.font = font;
        in.style.bold = bold;
        in.style.italic = italic;
        in.style.mirror = mirror;
        double dep = 0.0;
        auto opt = [&](const ParamSlot &s, double &v, ValueKind k) { return s.empty() || ctx.value(s, v, st, k); };
        if(!ctx.value(size, in.style.size, st) || !opt(letterSpacing, in.style.letterSpacing, ValueKind::Length) ||
           !opt(lineSpacing, in.style.lineSpacing, ValueKind::Scalar) || !opt(x, in.x, ValueKind::Length) ||
           !opt(y, in.y, ValueKind::Length) || !opt(rotation, in.rotation, ValueKind::Angle) || !ctx.value(depth, dep, st))
            return {input, st};
        if(in.style.size <= 0.0) return {input, Status::error("the text size must be more than 0")};
        if(dep <= 0.0) return {input, Status::error("the depth must be more than 0")};
        if(in.style.lineSpacing <= 0.0) return {input, Status::error("the line spacing must be more than 0")};

        const auto shape = buildText(text, in.style);
        if(!shape->ok) return {input, Status::error(shape->error)};
        if(!shape->warning.empty()) st.merge(Status::warning(shape->warning));
        const TextOnFace laid = layTextOnFace(target, in);
        if(!laid.ok) return {input, Status::error(laid.error)};
        if(!laid.warning.empty() && laid.warning != shape->warning) st.merge(Status::warning(laid.warning));

        const bool engrave = direction == TextDirection::Engrave;
        // Past the surface on the open side, and a little into the body under raised text.
        const double margin = std::max(0.2, 0.25 * dep);
        std::vector<TopoDS_Shape> solids;
        std::string why;
        if(!textSolids(laid.frame, *shape, in, engrave ? dep : std::min(0.1, 0.5 * dep), engrave ? margin : dep,
                       solids, why))
            return {input, Status::error(why)};
        // One tool per letter: kerned letters can overlap, and a boolean
        // argument must not overlap itself.
        const std::string prefix = "f" + std::to_string(id);
        std::vector<NamedShape> tools;
        for(size_t k = 0; k < solids.size(); ++k) {
            TopTools_IndexedMapOfShape faces;
            TopExp::MapShapes(solids[k], TopAbs_FACE, faces);
            std::vector<FaceLabel> labels;
            for(int i = 1; i <= faces.Extent(); ++i)
                labels.push_back({prefix + "/l" + std::to_string(k + 1) + "/" + std::to_string(i), {}});
            tools.emplace_back(solids[k], labels);
        }
        std::vector<const NamedShape *> toolPtrs;
        for(const auto &t : tools) toolPtrs.push_back(&t);
        const std::shared_ptr<const Body> shown = toolBody(NamedShape(makeCompound(solids), {}));

        const double before = volumeOf(r.body->shape.shape());
        BooleanResult br = runBoolean(engrave ? BoolOp::Cut : BoolOp::Fuse, {&r.body->shape}, toolPtrs, prefix);
        if(!br.ok) return {input, Status::error(br.error), shown};
        if(!validateResult(br.shape, prefix, st)) return {input, st, shown};
        const double after = volumeOf(br.shape.shape());
        // No more than the letters themselves can be cut or added.
        double letters = 0.0;
        for(const TopoDS_Shape &sol : solids) letters += std::fabs(volumeOf(sol));
        if(std::fabs(after - before) > letters * 1.02 + 1e-6)
            return {input, Status::error("the text could not be cut into this face cleanly"), shown};
        if(engrave ? after > before - 1e-9 : after < before + 1e-9)
            return {input, Status::error(engrave ? "the text does not cut into the body here"
                                                 : "the text does not add to the body here"),
                    shown};
        auto out = std::make_shared<ModelState>(*input);
        const std::vector<BodyId> made = replaceBody(*out, r.body->id, br.shape);
        if(made.size() > 1)
            st.merge(Status::warning(engrave ? "the text cuts the body into pieces"
                                             : "some letters are not joined to the body (they run past the face)"));
        return {out, st, shown};
    });
}

} // namespace cad
