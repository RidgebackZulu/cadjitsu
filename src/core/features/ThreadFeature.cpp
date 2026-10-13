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
                std::string &why, bool openStart, bool openEnd, const Countersink &csk) {
    const double c = internal ? clearance : -clearance;
    const double rMin = spec.minor() / 2 + c, rMaj = spec.major / 2 + c, p = spec.pitch;
    if(rMin <= 0 || length <= p) {
        why = "the thread is too short or too small";
        return false;
    }
    const double L = length, e = 0.05 * p;
    // Where a closed end lets the thread fade out.
    const double fade = std::min(2 * p, L / 3);
    const double rBig = rMaj + p;
    // How far the ridge runs on past an open face of a hole (not a whole
    // number of turns, so the helix never meets the face plane edge on).
    const double past = 0.37 * p;
    auto cylinder = [](double r, double z0, double z1) {
        return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, z0), gp_Dir(0, 0, 1)), r, z1 - z0).Shape();
    };

    // 0. Fit the cylinder to the thread, on its own axis: a hole wider than
    // the thread's root is filled over the length (and bored again below), a
    // post thinner than it is grown out to it.
    NamedShape fitted = body;
    if(internal ? outer > rMin + 1e-6 * p : outer < rMin - 1e-6 * p) {
        const double before = volumeOf(body.shape());
        bool ok = false;
        // The fill meets the old wall face to face; should OCCT not take it,
        // a fill a hair bigger (inside the part anyway) does.
        for(const double grow : {0.0, 0.002, 0.01}) {
            const double r = (internal ? outer : rMin) + grow;
            const NamedShape fill(placed(cylinder(r, 0, L), frame), {});
            BooleanResult br = runBoolean(BoolOp::Fuse, {&body}, {&fill}, prefix);
            if(!br.ok) continue;
            const double added = volumeOf(br.shape.shape()) - before;
            const double hole = kPiT * (outer * outer) * L, grown = kPiT * (rMin * rMin - outer * outer) * L;
            const double want = internal ? hole : grown;
            if(added < 0.9 * want || added > 1.1 * want + kPiT * 2 * r * grow * L + 1e-9) continue;
            if(!BRepCheck_Analyzer(br.shape.shape()).IsValid()) continue;
            fitted = br.shape;
            ok = true;
            break;
        }
        if(!ok) {
            why = internal ? "the hole could not be fitted to the " + spec.name + " thread"
                           : "the post could not be fitted to the " + spec.name + " thread";
            return false;
        }
    }

    // 1. The plain part: a hole opened to the root diameter, or a boss turned
    // down to it.
    TopoDS_Shape plain;
    if(internal) {
        plain = cylinder(rMin, openStart ? -e : 0, openEnd ? L + e : L);
    } else {
        plain = boolean(Op::Cut, cylinder(std::max(outer, rMaj) + 1.0, 0, L), cylinder(rMin, 0, L));
    }
    if(plain.IsNull()) {
        why = internal ? "the hole could not be opened for the thread" : "the boss could not be turned down";
        return false;
    }
    const NamedShape plainNamed(placed(plain, frame), {});
    BooleanResult first = runBoolean(BoolOp::Cut, {&fitted}, {&plainNamed}, prefix);
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
    if(envelope.IsNull()) {
        why = "the " + spec.name + " thread could not be built";
        return false;
    }
    // The part of the ridge that lies in the part: above the root, between the ends.
    const TopoDS_Shape rootCore = cylinder(rMin, -3 * p, L + 3 * p);
    const TopoDS_Shape slab = cylinder(rBig + p, 0, L);

    // 3. The ridge, swept past both ends and trimmed to the envelope: cut into
    // a hole's wall, or joined onto the turned boss. A boolean that silently
    // leaves the shape as it was is tried again with the ridge started
    // elsewhere round the axis.
    const double before = volumeOf(first.shape.shape());
    NamedShape threaded;
    bool done = false;
    for(int attempt = 0; attempt < 4 && !done; ++attempt) {
        const double sink = p * (0.1 + 0.03 * attempt);
        const TopoDS_Shape swept = ridge(p, rMin, rMaj, -2 * p, L + 2 * p, leftHand, sink, 0.7 + 1.9 * attempt);
        if(swept.IsNull()) continue;
        const TopoDS_Shape r = boolean(Op::Common, swept, envelope);
        if(r.IsNull()) continue;
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
        BooleanResult br = runBoolean(internal ? BoolOp::Cut : BoolOp::Fuse, {&first.shape}, {&ridgeNamed}, prefix);
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
            BooleanResult once = runBoolean(BoolOp::Cut, {&fitted}, {&bothNamed}, prefix);
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
    if(!internal || !csk.on || (!openStart && !openEnd)) {
        result = threaded;
        return true;
    }

    // 4. A hole's countersink at each open face: a cone of the included angle
    // from its diameter at the face (by default a little over the major, so
    // there is no thin, sharp first turn) on past the root into the bore's
    // air rather than ending on its wall, so a screw starts true.
    const double half = std::clamp(csk.angle, 30.0, 150.0) / 2 * kPiT / 180;
    const double rIn = std::max(0.5 * rMin, rMin - 0.2 * p);
    const double rWant = csk.diameter > 0 ? csk.diameter / 2 : rMaj + 0.15 * p;
    // No deeper than a third of the thread.
    const double rFace = std::min(rWant, rIn + std::tan(half) * L / 3);
    if(rFace <= rMin + 1e-6) {
        result = threaded;
        return true;
    }
    for(const double grow : {0.0, 0.03 * p, -0.03 * p}) {
        const double rl = rFace + grow, deep = (rl - rIn) / std::tan(half), over = e * std::tan(half);
        TopoDS_Shape cone;
        if(openStart) cone = revolved({{0, -e}, {rl + over, -e}, {rIn, deep}, {0, deep}});
        if(openEnd) {
            const TopoDS_Shape top = revolved({{0, L - deep}, {rIn, L - deep}, {rl + over, L + e}, {0, L + e}});
            cone = cone.IsNull() ? top : boolean(Op::Fuse, cone, top);
        }
        if(cone.IsNull()) continue;
        const double before2 = volumeOf(threaded.shape());
        const NamedShape coneNamed(placed(cone, frame), {});
        BooleanResult lead = runBoolean(BoolOp::Cut, {&threaded}, {&coneNamed}, prefix);
        if(!lead.ok) continue;
        const double cut = before2 - volumeOf(lead.shape.shape());
        // The cones take the first turn's crests (and more, when wide): more
        // than nothing, less than the whole ring of wall round them.
        const double ring = kPiT * (rl * rl - rMin * rMin) * (deep + e) * ((openStart ? 1 : 0) + (openEnd ? 1 : 0));
        if(cut <= 1e-6 * ring || cut > ring || !BRepCheck_Analyzer(lead.shape.shape()).IsValid()) continue;
        result = lead.shape;
        return true;
    }
    why = "the countersink for the " + spec.name + " thread could not be cut";
    return false;
}

std::vector<ParamDef> ThreadFeature::params() const {
    std::vector<ParamDef> out;
    if(!clearance.empty()) out.push_back({clearance.name, clearance.expr, ValueKind::Length, id, name + " Clearance"});
    if(!length.empty()) out.push_back({length.name, length.expr, ValueKind::Length, id, name + " Length"});
    if(!countersinkDiameter.empty())
        out.push_back({countersinkDiameter.name, countersinkDiameter.expr, ValueKind::Length, id, name + " Countersink Diameter"});
    return out;
}

json ThreadFeature::dataToJson() const {
    json fs = json::array();
    for(const auto &f : faces) fs.push_back(f.toJson());
    json j{{"faces", fs}, {"size", size}, {"clearance", clearance.toJson()}, {"countersink", countersink},
           {"countersinkAngle", countersinkAngle}};
    if(!length.empty()) j["length"] = length.toJson();
    if(!countersinkDiameter.empty()) j["countersinkDiameter"] = countersinkDiameter.toJson();
    if(leftHand) j["leftHand"] = true;
    return j;
}

void ThreadFeature::dataFromJson(const json &j) {
    faces.clear();
    for(const json &f : j.value("faces", json::array())) faces.push_back(TopoRef::fromJson(f));
    size = jget<std::string>(j, "size", "");
    countersink = jget<bool>(j, "countersink", true);
    countersinkAngle = jget<double>(j, "countersinkAngle", 90.0);
    countersinkDiameter = ParamSlot::fromJson(j.value("countersinkDiameter", json()));
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
        Countersink csk;
        csk.on = countersink;
        csk.angle = countersinkAngle;
        if(countersink && !countersinkDiameter.empty()) {
            if(!ctx.value(countersinkDiameter, csk.diameter, st)) return {input, st};
            if(csk.diameter <= 0) return {input, Status::error("the countersink diameter must be positive")};
        }

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
            const std::string tprefix = prefix + "/t" + std::to_string(i + 1);
            // The hole or post is fitted to the thread, then threaded.
            NamedShape result;
            std::string why;
            if(!threadBody(body->shape, *spec, frame, b - a, internal, clear, leftHand, radius, tprefix, result, why,
                           startOpen, endOpen, csk))
                return {input, Status::error(why)};
            if(!validateResult(result, prefix, st)) return {input, st};
            replaceBody(*out, body->id, result);
        }
        return {out, st};
    });
}

} // namespace cad
