#include "features/ThreadFeature.h"

#include "features/BodyOps.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools.hxx>
#include <Geom2d_Line.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <TopoDS.hxx>

#include <TopTools_ListOfShape.hxx>

#include <cmath>
#include <memory>
#include <utility>
#include <vector>
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
// P/8 flat at the crest) swept along a helix around Z from z = `from` to `to`,
// sunk `sink` below the root radius so it overlaps the core. `turn` rotates
// where it starts (a retry when a boolean trips on the geometry).
TopoDS_Shape ridge(double pitch, double rRoot, double rCrest, double from, double to, bool leftHand, double sink, double turn) {
    const double flank = std::tan(kPiT / 6);
    const double half = 3 * pitch / 8 + sink * flank;
    const double z0 = from + half, z1 = to - half;
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

// A solid of revolution about Z from an outline in the (r, z) half plane,
// from the axis round and back to it.
TopoDS_Shape revolved(const std::vector<std::pair<double, double>> &rz) {
    BRepBuilderAPI_MakePolygon poly;
    for(const auto &[r, z] : rz) poly.Add(gp_Pnt(r, 0, z));
    poly.Close();
    BRepBuilderAPI_MakeFace face(poly.Wire(), Standard_True);
    if(!face.IsDone()) return {};
    BRepPrimAPI_MakeRevol rev(face.Face(), gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 2 * kPiT);
    rev.Build();
    return rev.IsDone() ? rev.Shape() : TopoDS_Shape();
}

TopoDS_Shape placed(const TopoDS_Shape &s, const gp_Ax3 &frame) {
    gp_Trsf place;
    place.SetDisplacement(gp_Ax3(), frame);
    return BRepBuilderAPI_Transform(s, place, Standard_True).Shape();
}

} // namespace

bool threadBody(const NamedShape &body, const ThreadSpec &spec, const gp_Ax3 &frame, double length, bool internal,
                double clearance, bool leftHand, double outer, const std::string &prefix, NamedShape &result,
                std::string &why, bool openStart, bool openEnd) {
    const double c = internal ? clearance : -clearance;
    const double rMin = spec.minor() / 2 + c, rMaj = spec.major / 2 + c, p = spec.pitch;
    if(rMin <= 0 || length <= p) {
        why = "the thread is too short or too small";
        return false;
    }
    // A hole drilled out past the thread's crests has nothing left to cut.
    if(internal && outer >= rMaj - 0.05 * p) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "the %.2f mm hole is too wide for a %s thread: drill it at the %.2f mm tap drill, or pick a larger size",
                      2 * outer, spec.name.c_str(), spec.tapDrill);
        why = buf;
        return false;
    }
    const double L = length, e = 0.05 * p;
    // The 45 degree lead-in at an open end of a hole: from a little over the
    // major diameter at the face down to the root (no thin, sharp first turn).
    const double rLead = std::min(rMaj + 0.15 * p, rMin + L / 3);
    // Where a closed end lets the thread fade out.
    const double fade = std::min(2 * p, L / 3);
    const double rBig = rMaj + p;
    // How far the ridge runs on past an open face of a hole (not a whole
    // number of turns, so the helix never meets the face plane edge on).
    const double past = 0.37 * p;

    // 1. The plain part: a hole opened to the root diameter (or to `rB`, when
    // it is already wider), or a boss turned down to it.
    auto plainPart = [&](double rB) -> TopoDS_Shape {
        if(internal) {
            const double z0 = openStart ? -e : 0, z1 = openEnd ? L + e : L;
            return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, z0), gp_Dir(0, 0, 1)), rB, z1 - z0).Shape();
        }
        const TopoDS_Shape tube = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), std::max(outer, rMaj) + 1.0, L).Shape();
        const TopoDS_Shape core = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), rMin, L).Shape();
        return boolean(Op::Cut, tube, core);
    };
    TopoDS_Shape plain = plainPart(rMin);
    if(plain.IsNull()) {
        why = internal ? "the hole could not be opened for the thread" : "the boss could not be turned down";
        return false;
    }
    const NamedShape plainNamed(placed(plain, frame), {});
    BooleanResult first = runBoolean(BoolOp::Cut, {&body}, {&plainNamed}, prefix);
    if(!first.ok) {
        why = first.error;
        return false;
    }
    // 2. What the ridge may fill: the full thread along the length; at an open
    // end out through the face (a hole) or down a 45 degree chamfer to the
    // root at the tip (a boss); at a closed end fading to the root.
    std::vector<std::pair<double, double>> env;
    if(openStart && internal) env.insert(env.end(), {{0, -past}, {rBig, -past}});
    else if(openStart) env.insert(env.end(), {{0, 0}, {rMin, 0}, {rBig, rBig - rMin}});
    else env.insert(env.end(), {{0, 0}, {rMin, 0}, {rMaj, fade}, {rBig, fade}});
    if(openEnd && internal) env.insert(env.end(), {{rBig, L + past}, {0, L + past}});
    else if(openEnd) env.insert(env.end(), {{rBig, L - (rBig - rMin)}, {rMin, L}, {0, L}});
    else env.insert(env.end(), {{rBig, L - fade}, {rMaj, L - fade}, {rMin, L}, {0, L}});
    const TopoDS_Shape envelope = revolved(env);
    const TopoDS_Shape slab = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), rBig + p, L).Shape();
    if(envelope.IsNull()) {
        why = "the " + spec.name + " thread could not be built";
        return false;
    }

    // 3. The ridge, swept past both ends and trimmed to the envelope: cut into
    // a hole's wall, or joined onto the turned boss. A boolean that silently
    // leaves the shape as it was is tried again with the ridge started
    // elsewhere round the axis (and a hole drilled wider than the root bored
    // out a few hundredths more: a wall cylinder the helical flanks cross is
    // where OCCT can miss the intersection altogether).
    const bool wide = internal && outer > rMin - 0.05 * p;
    NamedShape threaded;
    bool done = false;
    constexpr int kAttempts = 10;
    for(int attempt = 0; attempt < (wide ? kAttempts : 4) && !done; ++attempt) {
        const double rB = wide ? outer + 0.013 * p * (attempt % 5) : rMin;
        BooleanResult bored = first;
        if(wide && attempt > 0) {
            plain = plainPart(rB);
            const NamedShape grown(placed(plain, frame), {});
            bored = runBoolean(BoolOp::Cut, {&body}, {&grown}, prefix);
            if(!bored.ok) continue;
        }
        const double before = volumeOf(bored.shape.shape());
        const double sink = p * (0.1 + 0.03 * (attempt % 4));
        const TopoDS_Shape swept = ridge(p, rMin, rMaj, -2 * p, L + 2 * p, leftHand, sink, 0.7 + 1.9 * attempt);
        if(swept.IsNull()) continue;
        TopoDS_Shape r = boolean(Op::Common, swept, envelope);
        if(r.IsNull()) continue;
        // Only the part of the ridge out past the wall goes into the part: cut
        // with that (from a little inside the wall, by a margin that varies
        // between attempts), not with the whole ridge.
        const double rWall = std::max(rMin, rB);
        static constexpr double kTrim[kAttempts] = {0.03, 0.1, 0.05, 0.15, 0.02, 0.07, 0.03, 0.04, 0.12, 0.03};
        const double trim = kTrim[attempt];
        if(wide) {
            const TopoDS_Shape core = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -3 * p), gp_Dir(0, 0, 1)), rWall - trim * p, L + 6 * p).Shape();
            r = boolean(Op::Cut, r, core);
            if(r.IsNull()) continue;
        }
        // The part of the ridge that lies in the part: out past the root (or
        // the wall), between the ends.
        const TopoDS_Shape rootCore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -3 * p), gp_Dir(0, 0, 1)), rWall, L + 6 * p).Shape();
        const TopoDS_Shape above = boolean(Op::Cut, r, rootCore);
        if(above.IsNull()) continue;
        const TopoDS_Shape inside = boolean(Op::Common, above, slab);
        if(inside.IsNull()) continue;
        const double expected = volumeOf(inside);
        // The ridge above the root must have gone in or out.
        auto took = [&](const NamedShape &s) {
            const double change = std::fabs(volumeOf(s.shape()) - before);
            return change >= 0.8 * expected && change <= 1.05 * expected && BRepCheck_Analyzer(s.shape()).IsValid();
        };
        const NamedShape ridgeNamed(placed(r, frame), {});
        BooleanResult br = runBoolean(internal ? BoolOp::Cut : BoolOp::Fuse, {&bored.shape}, {&ridgeNamed}, prefix);
        if(br.ok && took(br.shape)) {
            threaded = br.shape;
            done = true;
            break;
        }
        // A hole: the ridge and the bore in one cutter, cut from the part at once.
        if(internal) {
            const TopoDS_Shape both = boolean(Op::Fuse, plain, r);
            if(both.IsNull()) continue;
            const NamedShape bothNamed(placed(both, frame), {});
            BooleanResult once = runBoolean(BoolOp::Cut, {&body}, {&bothNamed}, prefix);
            if(once.ok && took(once.shape)) {
                threaded = once.shape;
                done = true;
            }
        }
    }
    if(!done) {
        why = "the " + spec.name + " thread could not be built";
        return false;
    }
    if(!internal || (!openStart && !openEnd)) {
        result = threaded;
        return true;
    }

    // 4. A hole's lead-in: a 45 degree countersink at each open face, from a
    // little over the major diameter down past the root, so a screw starts
    // true. It runs on into the bore's air rather than ending on its wall.
    const double rIn = std::max(0.5 * rMin, rMin - 0.2 * p);
    for(const double grow : {0.0, 0.03 * p, -0.03 * p}) {
        const double rl = rLead + grow, deep = rl - rIn;
        TopoDS_Shape cone;
        if(openStart) cone = revolved({{0, -e}, {rl + e, -e}, {rIn, deep}, {0, deep}});
        if(openEnd) {
            const TopoDS_Shape top = revolved({{0, L - deep}, {rIn, L - deep}, {rl + e, L + e}, {0, L + e}});
            cone = cone.IsNull() ? top : boolean(Op::Fuse, cone, top);
        }
        if(cone.IsNull()) continue;
        const double before2 = volumeOf(threaded.shape());
        const NamedShape coneNamed(placed(cone, frame), {});
        BooleanResult lead = runBoolean(BoolOp::Cut, {&threaded}, {&coneNamed}, prefix);
        if(!lead.ok) continue;
        const double cut = before2 - volumeOf(lead.shape.shape());
        // The cones take the crests of the first turn: more than nothing, less
        // than a full ring of the wall around them.
        const double ring = kPiT * (rl * rl - rMin * rMin) * (rl - rMin) * ((openStart ? 1 : 0) + (openEnd ? 1 : 0));
        if(cut <= 1e-6 * ring || cut > ring || !BRepCheck_Analyzer(lead.shape.shape()).IsValid()) continue;
        result = lead.shape;
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
            const bool topOpen = open(v1, 1), bottomOpen = open(v0, -1);
            if(len > 0) {
                if(topOpen || !bottomOpen) a = std::max(v0, v1 - len);
                else b = std::min(v1, v0 + len);
            }
            // The thread runs out through an end of the face that is open; a
            // part-length thread's inner end, or a blind end, fades out.
            const bool startOpen = a <= v0 + 1e-9 && bottomOpen, endOpen = b >= v1 - 1e-9 && topOpen;
            gp_Ax3 frame(onAxis(a), dir, cyl.Position().XDirection());
            const bool model = mode == ThreadMode::Modeled || (mode == ThreadMode::Auto && spec->modelByDefault());
            const std::string tprefix = prefix + "/t" + std::to_string(i + 1);
            NamedShape result;
            if(model) {
                // Drilled wider than the tap drill: the thread is shallower than it should be.
                if(internal && 2 * radius > spec->tapDrill + 0.1 && radius < spec->major / 2 + clear) {
                    char buf[200];
                    std::snprintf(buf, sizeof buf, "the %.2f mm hole is wider than the %.2f mm %s tap drill: the thread is shallow",
                                  2 * radius, spec->tapDrill, spec->name.c_str());
                    st.merge(Status::warning(buf));
                }
                std::string why;
                if(!threadBody(body->shape, *spec, frame, b - a, internal, clear, leftHand, radius, tprefix, result, why,
                               startOpen, endOpen))
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
