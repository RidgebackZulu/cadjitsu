#include "features/ThreadFeature.h"

#include "features/BodyOps.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <TopoDS.hxx>

#include <TopTools_ListOfShape.hxx>

#include <cmath>
#include <memory>
#include <cstdio>
#include <cstdlib>

namespace cad {

namespace {

constexpr double kPiT = 3.14159265358979323846;

enum class Op { Fuse, Cut, Common };

// One boolean, built once (constructing the OCCT operation with its shapes
// would build it straight away, before the options are set).
TopoDS_Shape boolean(Op which, const TopoDS_Shape &a, const TopoDS_Shape &b) {
    std::unique_ptr<BRepAlgoAPI_BooleanOperation> op;
    switch(which) {
    case Op::Fuse: op = std::make_unique<BRepAlgoAPI_Fuse>(); break;
    case Op::Cut: op = std::make_unique<BRepAlgoAPI_Cut>(); break;
    case Op::Common: op = std::make_unique<BRepAlgoAPI_Common>(); break;
    }
    TopTools_ListOfShape args, tools;
    args.Append(a);
    tools.Append(b);
    op->SetArguments(args);
    op->SetTools(tools);
    op->SetFuzzyValue(kBooleanFuzz);
    op->SetRunParallel(Standard_False);
    op->Build();
    if(!op->IsDone() || op->HasErrors()) return {};
    return op->Shape();
}

// The thread's ridge alone: the ISO basic profile (3/4 P wide at the root,
// P/8 flat at the crest) swept along a helix around Z, sunk `sink` below the
// root radius so it overlaps the core. It stays within z = 0..length (the
// profile's half width short of each end), so it needs no trimming. `turn`
// rotates where it starts (a retry when a boolean trips on the geometry).
TopoDS_Shape ridge(double pitch, double rRoot, double rCrest, double length, bool leftHand, double sink, double turn) {
    const double flank = std::tan(kPiT / 6);
    const double half = 3 * pitch / 8 + sink * flank;
    const double z0 = half + 0.02 * pitch, z1 = length - half - 0.02 * pitch;
    if(z1 <= z0) return {};
    const double turns = (z1 - z0) / pitch;
    // The spine runs inside the root (not on a surface a boolean would find coincident).
    Handle(Geom_CylindricalSurface) cyl =
        new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0, 0, z0), gp_Dir(0, 0, 1), gp_Dir(std::cos(turn), std::sin(turn), 0)),
                                    rRoot * 0.5);
    Handle(Geom2d_Line) line = new Geom2d_Line(gp_Pnt2d(0, 0), gp_Dir2d(leftHand ? -2 * kPiT : 2 * kPiT, pitch));
    TopoDS_Edge helix = BRepBuilderAPI_MakeEdge(line, cyl, 0, turns * std::hypot(2 * kPiT, pitch));
    BRepLib::BuildCurves3d(helix);
    const gp_Dir out(std::cos(turn), std::sin(turn), 0);
    auto at = [&](double r, double z) { return gp_Pnt(out.X() * r, out.Y() * r, z0 + z); };
    BRepBuilderAPI_MakePolygon poly;
    poly.Add(at(rRoot - sink, -half));
    poly.Add(at(rCrest, -pitch / 16));
    poly.Add(at(rCrest, pitch / 16));
    poly.Add(at(rRoot - sink, half));
    poly.Close();
    BRepOffsetAPI_MakePipeShell pipe(BRepBuilderAPI_MakeWire(helix).Wire());
    pipe.SetMode(gp_Dir(0, 0, 1)); // the profile stays in planes through the axis
    pipe.Add(poly.Wire(), Standard_False, Standard_False);
    pipe.Build();
    if(!pipe.IsDone() || !pipe.MakeSolid()) return {};
    return pipe.Shape();
}

TopoDS_Shape placed(const TopoDS_Shape &s, const gp_Ax3 &frame) {
    gp_Trsf place;
    place.SetDisplacement(gp_Ax3(), frame);
    return BRepBuilderAPI_Transform(s, place, Standard_True).Shape();
}

} // namespace

bool threadBody(const NamedShape &body, const ThreadSpec &spec, const gp_Ax3 &frame, double length, bool internal,
                double clearance, bool leftHand, double outer, const std::string &prefix, NamedShape &result,
                std::string &why) {
    const double c = internal ? clearance : -clearance;
    const double rMin = spec.minor() / 2 + c, rMaj = spec.major / 2 + c;
    if(rMin <= 0 || length <= spec.pitch) {
        why = "the thread is too short or too small";
        return false;
    }
    // 1. The plain part: a hole opened to the root diameter, or a boss turned down to it.
    TopoDS_Shape plain;
    if(internal) {
        plain = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), rMin, length).Shape();
    } else {
        const TopoDS_Shape tube = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), std::max(outer, rMaj) + 1.0, length).Shape();
        const TopoDS_Shape core = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), rMin, length).Shape();
        plain = boolean(Op::Cut, tube, core);
        if(plain.IsNull()) {
            why = "the boss could not be turned down";
            return false;
        }
    }
    const NamedShape plainNamed(placed(plain, frame), {});
    BooleanResult first = runBoolean(BoolOp::Cut, {&body}, {&plainNamed}, prefix);
    if(!first.ok) {
        why = first.error;
        return false;
    }
    // 2. The ridge: cut into a hole's wall, or joined onto the turned boss. A
    // boolean that silently leaves the shape as it was is tried again with the
    // ridge started elsewhere round the axis.
    const double before = volumeOf(first.shape.shape());
    for(int attempt = 0; attempt < 4; ++attempt) {
        const double sink = spec.pitch * (0.1 + 0.03 * attempt);
        const TopoDS_Shape r = ridge(spec.pitch, rMin, rMaj, length, leftHand, sink, 0.7 + 1.9 * attempt);
        if(r.IsNull()) continue;
        const double ridgeVolume = volumeOf(r);
        const NamedShape ridgeNamed(placed(r, frame), {});
        BooleanResult br = runBoolean(internal ? BoolOp::Cut : BoolOp::Fuse, {&first.shape}, {&ridgeNamed}, prefix);
        if(!br.ok) continue;
        const double change = std::fabs(volumeOf(br.shape.shape()) - before);
        // Most of the ridge (all but the sunk root) must have gone in or out.
        if(change < 0.5 * ridgeVolume || change > 1.05 * ridgeVolume) continue;
        result = br.shape;
        return true;
    }
    why = "the " + spec.name + " thread could not be built";
    return false;
}

const char *toString(ThreadMode m) {
    return m == ThreadMode::Modeled ? "modeled" : m == ThreadMode::TapDrill ? "tapDrill" : "auto";
}

ThreadMode threadModeFromString(const std::string &s) {
    return s == "modeled" ? ThreadMode::Modeled : s == "tapDrill" ? ThreadMode::TapDrill : ThreadMode::Auto;
}

std::vector<ParamDef> ThreadFeature::params() const {
    std::vector<ParamDef> out;
    if(!clearance.empty()) out.push_back({clearance.name, clearance.expr, ValueKind::Length, id, name + " Clearance"});
    if(!length.empty()) out.push_back({length.name, length.expr, ValueKind::Length, id, name + " Length"});
    return out;
}

json ThreadFeature::dataToJson() const {
    json fs = json::array();
    for(const auto &f : faces) fs.push_back(f.toJson());
    json j{{"faces", fs}, {"size", size}, {"mode", toString(mode)}, {"clearance", clearance.toJson()}};
    if(!length.empty()) j["length"] = length.toJson();
    if(leftHand) j["leftHand"] = true;
    return j;
}

void ThreadFeature::dataFromJson(const json &j) {
    faces.clear();
    for(const json &f : j.value("faces", json::array())) faces.push_back(TopoRef::fromJson(f));
    size = jget<std::string>(j, "size", "");
    mode = threadModeFromString(jget<std::string>(j, "mode", "auto"));
    clearance = ParamSlot::fromJson(j.value("clearance", json()));
    length = ParamSlot::fromJson(j.value("length", json()));
    leftHand = jget<bool>(j, "leftHand", false);
}

FeatureResult ThreadFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        if(faces.empty()) return {input, Status::error("select the cylindrical faces to thread")};
        double clear = 0.15, len = 0;
        if(!clearance.empty() && !ctx.value(clearance, clear, st)) return {input, st};
        if(!length.empty()) {
            if(!ctx.value(length, len, st)) return {input, st};
            if(len <= 0) return {input, Status::error("the thread length must be positive")};
        }
        if(clear < 0 || clear > 1.0) return {input, Status::error("the clearance must be between 0 and 1 mm")};
        const ThreadSpec *fixed = nullptr;
        if(!size.empty() && !(fixed = findThread(size))) return {input, Status::error("unknown thread size " + size)};

        auto out = std::make_shared<ModelState>(*input);
        const std::string prefix = "f" + std::to_string(id);
        for(size_t i = 0; i < faces.size(); ++i) {
            if(ctx.cancelled()) return {input, Status::error("cancelled")};
            const ResolvedRef r = resolveRef(*out, faces[i]);
            st.merge(r.status);
            if(!r.ok) return {input, st};
            const TopoDS_Face face = TopoDS::Face(r.shape);
            BRepAdaptor_Surface surf(face);
            if(surf.GetType() != GeomAbs_Cylinder)
                return {input, Status::error("face " + std::to_string(i + 1) + " is not cylindrical")};
            const gp_Cylinder cyl = surf.Cylinder();
            const double radius = cyl.Radius();
            double u0, u1, v0, v1;
            BRepTools::UVBounds(face, u0, u1, v0, v1);
            // A hole's wall faces its axis; a boss's faces away.
            const gp_Pnt mid = surf.Value((u0 + u1) / 2, (v0 + v1) / 2);
            gp_Dir n;
            const gp_Ax1 axis = cyl.Axis();
            const gp_Pnt foot = axis.Location().Translated(gp_Vec(axis.Direction()) * gp_Vec(axis.Location(), mid).Dot(gp_Vec(axis.Direction())));
            const bool internal = faceNormalAt(face, mid, n) && gp_Vec(foot, mid).Dot(gp_Vec(n)) < 0;
            const ThreadSpec *spec = fixed ? fixed : nearestThread(2 * radius, internal);
            if(!spec)
                return {input, Status::error("no standard thread fits a " + std::to_string(2 * radius).substr(0, 5) +
                                             " mm " + (internal ? "hole" : "boss") + "; choose a size")};
            // Where along the axis: the whole face, or `length` from its open end.
            const gp_Pnt base = cyl.Position().Location();
            const gp_Dir dir = cyl.Position().Direction();
            auto onAxis = [&](double v) { return base.Translated(gp_Vec(dir) * v); };
            const Body *body = out->body(r.body->id);
            auto open = [&](double v, double outward) {
                BRepClass3d_SolidClassifier cls(body->shape.shape(), onAxis(v + outward * 0.5), 1e-6);
                return cls.State() == TopAbs_OUT;
            };
            double a = v0, b = v1;
            if(len > 0) {
                const bool topOpen = open(v1, 1), bottomOpen = open(v0, -1);
                if(topOpen || !bottomOpen) a = std::max(v0, v1 - len);
                else b = std::min(v1, v0 + len);
            }
            gp_Ax3 frame(onAxis(a), dir, cyl.Position().XDirection());
            const bool model = mode == ThreadMode::Modeled || (mode == ThreadMode::Auto && spec->modelByDefault());
            const std::string tprefix = prefix + "/t" + std::to_string(i + 1);
            NamedShape result;
            if(model) {
                std::string why;
                if(!threadBody(body->shape, *spec, frame, b - a, internal, clear, leftHand, radius, tprefix, result, why))
                    return {input, Status::error(why)};
            } else {
                TopoDS_Shape cutter;
                if(internal) {
                    // Just the tap drill (to tap, or for a self-tapping screw).
                    if(spec->tapDrill / 2 <= radius + 1e-6) {
                        st.merge(Status::warning("the hole is already at least the " + spec->name + " tap drill size"));
                        continue;
                    }
                    cutter = BRepPrimAPI_MakeCylinder(gp_Ax2(frame.Location(), dir), spec->tapDrill / 2, b - a).Shape();
                } else {
                    // A boss turned down to the thread's outside, less the clearance.
                    const double rOut = spec->major / 2 - clear;
                    if(rOut >= radius - 1e-6) continue;
                    const TopoDS_Shape big = BRepPrimAPI_MakeCylinder(gp_Ax2(frame.Location(), dir), radius + 1, b - a).Shape();
                    const TopoDS_Shape keep = BRepPrimAPI_MakeCylinder(gp_Ax2(frame.Location(), dir), rOut, b - a).Shape();
                    cutter = boolean(Op::Cut, big, keep);
                }
                const NamedShape cutterNamed(cutter, {});
                BooleanResult br = runBoolean(BoolOp::Cut, {&body->shape}, {&cutterNamed}, tprefix);
                if(!br.ok) return {input, Status::error(br.error)};
                result = br.shape;
            }
            if(!validateResult(result, prefix, st)) return {input, st};
            replaceBody(*out, body->id, result);
        }
        return {out, st};
    });
}

} // namespace cad
