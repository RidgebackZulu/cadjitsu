#include "features/SplitFeature.h"

#include "features/BodyOps.h"
#include "geom/OcctUtil.h"
#include "sketch/SketchResult.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

const char *keepName(SplitKeep k) { return k == SplitKeep::Front ? "front" : k == SplitKeep::Back ? "back" : "both"; }
SplitKeep keepFromName(const std::string &s) {
    return s == "front" ? SplitKeep::Front : s == "back" ? SplitKeep::Back : SplitKeep::Both;
}

gp_Pnt centreOfMass(const TopoDS_Shape &s) {
    GProp_GProps g;
    BRepGProp::VolumeProperties(s, g);
    return g.CentreOfMass();
}

// The sketch's curves as edges in the model.
std::vector<TopoDS_Edge> sketchEdges(const SketchResult &sk, const std::vector<int> &ids) {
    std::vector<TopoDS_Edge> out;
    const Sketch &s = sk.sketch;
    auto pt = [&](int id) -> std::optional<gp_Pnt> {
        const SkEntity *e = s.find(id);
        if(!e) return std::nullopt;
        return sk.toWorld({e->x, e->y});
    };
    for(const SkEntity &e : s.entities) {
        if(!e.isCurve() || e.id < 0) continue;
        if(ids.empty() ? e.construction : std::find(ids.begin(), ids.end(), e.id) == ids.end()) continue;
        const gp_Ax2 ax(gp_Pnt(0, 0, 0), sk.frame.Direction(), sk.frame.XDirection());
        if(e.type == SkType::Line) {
            const auto a = pt(e.a), b = pt(e.b);
            if(a && b && a->Distance(*b) > 1e-9) out.push_back(BRepBuilderAPI_MakeEdge(*a, *b).Edge());
        } else if(e.type == SkType::Circle) {
            const auto c = pt(e.a);
            if(c && e.r > 1e-9) out.push_back(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(*c, ax.Direction(), ax.XDirection()), e.r)).Edge());
        } else if(e.type == SkType::Arc) {
            const auto c = pt(e.a), p1 = pt(e.b), p2 = pt(e.c);
            if(!c || !p1 || !p2) continue;
            const double r = c->Distance(*p1);
            if(r < 1e-9) continue;
            BRepBuilderAPI_MakeEdge me(gp_Circ(gp_Ax2(*c, ax.Direction(), ax.XDirection()), r), *p1, *p2);
            if(me.IsDone()) out.push_back(me.Edge());
        }
    }
    return out;
}

// Where alignment pins go on a flat cut face: two points far apart along the
// face's long direction, each with room for a pin hole and a wall around it.
std::vector<gp_Pnt> pinSpots(const TopoDS_Face &face, const gp_Ax3 &frame, double r) {
    const double wall = std::max(1.2, r * 0.8);
    // The face's extent in plane coordinates.
    const Bnd_Box full = boundingBox(face);
    if(full.IsVoid()) return {};
    double x0, y0, z0, x1, y1, z1;
    full.Get(x0, y0, z0, x1, y1, z1);
    const gp_Pnt mid((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    const gp_Pnt c = centroidOfFace(face);
    // The long direction in the plane: whichever frame axis the face spans most.
    double spanX = 0, spanY = 0;
    for(double sx : {x0, x1})
        for(double sy : {y0, y1})
            for(double sz : {z0, z1}) {
                const gp_Vec v(mid, gp_Pnt(sx, sy, sz));
                spanX = std::max(spanX, std::fabs(v.Dot(gp_Vec(frame.XDirection()))));
                spanY = std::max(spanY, std::fabs(v.Dot(gp_Vec(frame.YDirection()))));
            }
    const gp_Dir along = spanX >= spanY ? frame.XDirection() : frame.YDirection();
    const double span = std::max(spanX, spanY);
    auto fits = [&](const gp_Pnt &p) {
        BRepClass_FaceClassifier cls(face, p, 1e-6);
        if(cls.State() != TopAbs_IN) return false;
        BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(p).Shape(), BRepTools::OuterWire(face));
        if(!d.IsDone() || d.Value() < r + wall) return false;
        // Inner loops (holes in the cut face) too.
        for(TopExp_Explorer ex(face, TopAbs_WIRE); ex.More(); ex.Next()) {
            BRepExtrema_DistShapeShape di(BRepBuilderAPI_MakeVertex(p).Shape(), ex.Current());
            if(di.IsDone() && di.Value() < r + wall) return false;
        }
        return true;
    };
    for(double t : {0.35, 0.3, 0.25, 0.2, 0.15, 0.1}) {
        const gp_Pnt a = c.Translated(gp_Vec(along) * (-t * span)), b = c.Translated(gp_Vec(along) * (t * span));
        if(fits(a) && fits(b)) return {a, b};
    }
    if(fits(c)) return {c};
    return {};
}

} // namespace

std::vector<ParamDef> SplitFeature::params() const {
    std::vector<ParamDef> out;
    if(pins && !pinDiameter.empty())
        out.push_back({pinDiameter.name, pinDiameter.expr, ValueKind::Length, id, name + " Pin Diameter"});
    if(pins && !pinDepth.empty()) out.push_back({pinDepth.name, pinDepth.expr, ValueKind::Length, id, name + " Pin Depth"});
    return out;
}

json SplitFeature::dataToJson() const {
    json j{{"bodies", bodies}, {"tool", tool == SplitTool::Plane ? "plane" : "sketch"}, {"keep", keepName(keep)}};
    if(tool == SplitTool::Plane) j["plane"] = plane.toJson();
    else {
        j["sketch"] = sketch;
        j["curves"] = curves;
    }
    if(pins) {
        j["pins"] = true;
        j["pinDiameter"] = pinDiameter.toJson();
        j["pinDepth"] = pinDepth.toJson();
    }
    return j;
}

void SplitFeature::dataFromJson(const json &j) {
    bodies = jget<std::vector<std::string>>(j, "bodies", {});
    tool = jget<std::string>(j, "tool", "plane") == "sketch" ? SplitTool::Sketch : SplitTool::Plane;
    plane = PlaneRef::fromJson(j.value("plane", json()));
    sketch = jget<int>(j, "sketch", kNoFeature);
    curves = jget<std::vector<int>>(j, "curves", {});
    keep = keepFromName(jget<std::string>(j, "keep", "both"));
    pins = jget<bool>(j, "pins", false);
    pinDiameter = ParamSlot::fromJson(j.value("pinDiameter", json()));
    pinDepth = ParamSlot::fromJson(j.value("pinDepth", json()));
}

std::vector<FeatureId> SplitFeature::dependencies() const {
    if(tool == SplitTool::Sketch) return {sketch};
    if(plane.kind == PlaneRef::Kind::Construction) return {plane.plane};
    return {};
}

FeatureResult SplitFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        const std::string prefix = "f" + std::to_string(id);

        // The bodies to split.
        std::vector<BodyId> targets;
        for(const BodyId &b : bodies) {
            const BodyId real = input->resolveBodyId(b);
            if(real.empty()) st.merge(Status::warning("body " + b + " no longer exists"));
            else if(std::find(targets.begin(), targets.end(), real) == targets.end()) targets.push_back(real);
        }
        if(!bodies.empty() && targets.empty()) return {input, Status::error("the bodies to split no longer exist")};

        // A size that reaches past every body.
        Bnd_Box all;
        for(const auto &kv : input->bodies) all.Add(boundingBox(kv.second->shape.shape()));
        if(all.IsVoid()) return {input, Status::error("there is no body to split")};
        double x0, y0, z0, x1, y1, z1;
        all.Get(x0, y0, z0, x1, y1, z1);
        const gp_Pnt mid((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
        // Half the bodies' diagonal, with a margin: the tool spans every body (and its preview stays near them).
        const double reach = gp_Pnt(x0, y0, z0).Distance(gp_Pnt(x1, y1, z1)) * 0.55 + 2.0;

        // The cutting tool.
        TopoDS_Shape cutter;
        gp_Ax3 frame;
        if(tool == SplitTool::Plane) {
            if(!resolvePlane(*input, plane, frame, st)) return {input, st};
            // Centred over the bodies.
            const gp_Vec off(frame.Location(), mid);
            const gp_Pnt centre = mid.Translated(-gp_Vec(frame.Direction()) * off.Dot(gp_Vec(frame.Direction())));
            frame.SetLocation(centre);
            cutter = BRepBuilderAPI_MakeFace(gp_Pln(frame), -reach, reach, -reach, reach).Face();
        } else {
            auto it = input->sketches.find(sketch);
            if(it == input->sketches.end()) return {input, Status::error("the sketch to split with no longer exists")};
            const SketchResult &sk = *it->second;
            const std::vector<TopoDS_Edge> edges = sketchEdges(sk, curves);
            if(edges.empty()) return {input, Status::error("the sketch has no curves to split with")};
            frame = sk.frame;
            const gp_Vec n(sk.frame.Direction());
            // Through the bodies, however far they are from the sketch plane.
            const double depth = std::fabs(gp_Vec(sk.frame.Location(), mid).Dot(n)) + reach;
            std::vector<TopoDS_Shape> faces;
            gp_Trsf down;
            down.SetTranslation(-n * depth);
            for(const TopoDS_Edge &e : edges) {
                const TopoDS_Shape moved = BRepBuilderAPI_Transform(e, down, true).Shape();
                faces.push_back(BRepPrimAPI_MakePrism(moved, n * (2 * depth)).Shape());
            }
            cutter = makeCompound(faces);
        }
        const std::shared_ptr<const Body> shown = toolBody(NamedShape(cutter, {}));
        if(targets.empty()) targets = bodiesInteracting(*input, cutter, true);
        if(targets.empty()) return {input, Status::error("the splitting tool does not cross any body"), shown};

        double pinD = 0, pinDeep = 0;
        if(pins) {
            if(tool != SplitTool::Plane)
                st.merge(Status::warning("alignment pins are only made for plane splits"));
            else if(!ctx.value(pinDiameter, pinD, st) || !ctx.value(pinDepth, pinDeep, st))
                return {input, st, shown};
        }

        auto out = std::make_shared<ModelState>(*input);
        for(const BodyId &t : targets) {
            if(ctx.cancelled()) return {input, Status::error("cancelled")};
            const Body *body = input->body(t);
            BRepAlgoAPI_Splitter split;
            TopTools_ListOfShape args, tools;
            args.Append(body->shape.shape());
            tools.Append(cutter);
            split.SetArguments(args);
            split.SetTools(tools);
            split.SetNonDestructive(Standard_True);
            split.SetFuzzyValue(kBooleanFuzz);
            split.Build();
            if(split.HasErrors() || !split.IsDone()) {
                st.merge(Status::warning("could not split " + body->name));
                continue;
            }
            NamedShape named = propagateNames(split.Shape(), {&body->shape}, split.History(), prefix);
            std::vector<TopoDS_Solid> pieces = solidsOf(named.shape());
            if(pieces.size() < 2) {
                st.merge(Status::warning("the splitting tool does not cross " + body->name));
                continue;
            }
            // Keep one side only.
            if(keep != SplitKeep::Both && tool == SplitTool::Plane) {
                std::vector<TopoDS_Shape> kept;
                for(const TopoDS_Solid &p : pieces) {
                    const double side = gp_Vec(frame.Location(), centreOfMass(p)).Dot(gp_Vec(frame.Direction()));
                    if((side > 0) == (keep == SplitKeep::Front)) kept.push_back(p);
                }
                if(kept.empty()) {
                    st.merge(Status::warning("nothing of " + body->name + " is on the kept side"));
                    continue;
                }
                named = named.subShape(makeCompound(kept));
            }
            // Alignment pins: holes across the cut, into both halves.
            if(pins && tool == SplitTool::Plane && keep == SplitKeep::Both && pinD > 0 && pinDeep > 0) {
                TopoDS_Face cutFace;
                double best = 0;
                for(TopExp_Explorer ex(named.shape(), TopAbs_FACE); ex.More(); ex.Next()) {
                    const TopoDS_Face f = TopoDS::Face(ex.Current());
                    gp_Pln pl;
                    if(!planeOfFace(f, pl) || !pl.Axis().Direction().IsParallel(frame.Direction(), 1e-6)) continue;
                    if(std::fabs(gp_Vec(frame.Location(), centroidOfFace(f)).Dot(gp_Vec(frame.Direction()))) > 1e-4) continue;
                    GProp_GProps g;
                    BRepGProp::SurfaceProperties(f, g);
                    if(g.Mass() > best) {
                        best = g.Mass();
                        cutFace = f;
                    }
                }
                const std::vector<gp_Pnt> spots = cutFace.IsNull() ? std::vector<gp_Pnt>{}
                                                                   : pinSpots(cutFace, frame, pinD / 2);
                if(spots.empty()) {
                    st.merge(Status::warning("no room for alignment pins on " + body->name + "'s cut"));
                } else {
                    std::vector<TopoDS_Shape> drills;
                    const gp_Vec n(frame.Direction());
                    for(const gp_Pnt &p : spots)
                        drills.push_back(BRepPrimAPI_MakeCylinder(gp_Ax2(p.Translated(-n * pinDeep), frame.Direction()),
                                                                  pinD / 2, 2 * pinDeep)
                                             .Shape());
                    const NamedShape drillShape(makeCompound(drills), {});
                    BooleanResult br = runBoolean(BoolOp::Cut, {&named}, {&drillShape}, prefix + "/pin");
                    if(br.ok) {
                        named = br.shape;
                    } else {
                        st.merge(Status::warning("the alignment pin holes could not be cut"));
                    }
                }
            }
            if(!validateResult(named, prefix, st)) return {input, st, shown};
            replaceBody(*out, t, named);
        }
        return {out, st, shown};
    });
}

} // namespace cad
