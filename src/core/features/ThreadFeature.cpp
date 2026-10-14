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
TopoDS_Shape boolean(Op which, const TopoDS_Shape &a, const TopoDS_Shape &b, const std::atomic<bool> *cancel = nullptr) {
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
    op->SetRunParallel(Standard_True); // threads' helical faces make long booleans: use the cores
    if(cancel) {
        Handle(CancelProgress) progress = new CancelProgress(cancel);
        op->Build(progress->Start());
        if(cancel->load()) return {};
    } else {
        op->Build();
    }
    if(!op->IsDone() || op->HasErrors()) return {};
    return op->Shape();
}

// The thread's ridge alone: the ISO basic profile (3/4 P wide at the root,
// P/8 flat at the crest) swept along a helix around Z from z = `from` to `to`.
// Below the root its flanks run on for `sink`, then straight in for another
// half pitch: it overlaps the core well clear of the root cylinder, so a
// boolean with that cylinder only meets the flanks, square on (a face just
// under the root, nearly on the cylinder, makes booleans slow and unsure).
// It is made of pieces of up to `perPiece` turns, each overlapping the next a
// little (to be given to a boolean as separate tools): OCCT's booleans miss
// the crossings of a single sweep of many turns with a cylinder (wrapping its
// seam again and again) and leave the part untouched, where short pieces each
// cross it simply. `turn` rotates where it starts; a retry when a boolean
// trips on the geometry changes both.
struct RidgePiece {
    TopoDS_Shape shape;
    double zLo, zHi; // the heights it spans
};
std::vector<RidgePiece> ridge(double pitch, double rRoot, double rCrest, double from, double to, bool leftHand, double sink,
                              double turn, double perPiece) {
    const double flank = std::tan(kPiT / 6);
    const double half = 3 * pitch / 8 + sink * flank;
    const double z0 = from + half, z1 = to - half;
    if(z1 <= z0) return {};
    const double turns = (z1 - z0) / pitch;
    const double rIn = std::max(0.3 * rRoot, rRoot - sink - pitch / 2);
    std::vector<RidgePiece> out;
    // The spine runs inside the root (not on a surface a boolean would find coincident).
    Handle(Geom_CylindricalSurface) cyl =
        new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0, 0, z0), gp_Dir(0, 0, 1), gp_Dir(std::cos(turn), std::sin(turn), 0)),
                                    rRoot * 0.5);
    const double perTurn = std::hypot(2 * kPiT, pitch); // helix length parameter per turn
    Handle(Geom2d_Line) line = new Geom2d_Line(gp_Pnt2d(0, 0), gp_Dir2d(leftHand ? -2 * kPiT : 2 * kPiT, pitch));
    const int pieces = std::max(1, int(std::ceil(turns / perPiece)));
    const double step = turns / pieces, overlap = 0.01; // turns
    for(int k = 0; k < pieces; ++k) {
        const double t0 = std::max(0.0, k * step - overlap), t1 = std::min(turns, (k + 1) * step + overlap);
        TopoDS_Edge helix = BRepBuilderAPI_MakeEdge(line, cyl, t0 * perTurn, t1 * perTurn);
        BRepLib::BuildCurves3d(helix);
        // The profile in the plane through the axis where the piece starts.
        const double a = turn + (leftHand ? -1 : 1) * 2 * kPiT * t0;
        const gp_Dir radial(std::cos(a), std::sin(a), 0);
        const double zs = z0 + pitch * t0;
        auto at = [&](double r, double z) { return gp_Pnt(radial.X() * r, radial.Y() * r, zs + z); };
        BRepBuilderAPI_MakePolygon poly;
        poly.Add(at(rIn, -half));
        poly.Add(at(rRoot - sink, -half));
        poly.Add(at(rCrest, -pitch / 16));
        poly.Add(at(rCrest, pitch / 16));
        poly.Add(at(rRoot - sink, half));
        poly.Add(at(rIn, half));
        poly.Close();
        BRepOffsetAPI_MakePipeShell pipe(BRepBuilderAPI_MakeWire(helix).Wire());
        pipe.SetMode(gp_Dir(0, 0, 1)); // the profile stays in planes through the axis
        pipe.Add(poly.Wire(), Standard_False, Standard_False);
        pipe.Build();
        if(!pipe.IsDone() || !pipe.MakeSolid()) return {};
        out.push_back({pipe.Shape(), zs - half, z0 + pitch * t1 + half});
    }
    return out;
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
                std::string &why, bool openStart, bool openEnd, const Countersink &csk, const std::atomic<bool> *cancel) {
    const double c = internal ? clearance : -clearance;
    const double rMin = spec.minor() / 2 + c, rMaj = spec.major / 2 + c, p = spec.pitch;
    if(rMin <= 0 || length <= p) {
        why = "the thread is too short or too small";
        return false;
    }
    auto stopped = [&] {
        if(!cancel || !cancel->load()) return false;
        why = "cancelled";
        return true;
    };
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
    bool filled = false;
    if(internal ? outer > rMin + 1e-6 * p : outer < rMin - 1e-6 * p) {
        const double before = volumeOf(body.shape());
        // The fill meets the old wall face to face; should OCCT not take it,
        // a fill a hair bigger (inside the part anyway) does.
        for(const double grow : {0.0, 0.002, 0.01}) {
            const double r = (internal ? outer : rMin) + grow;
            const NamedShape fill(placed(cylinder(r, 0, L), frame), {});
            BooleanResult br = runBoolean(BoolOp::Fuse, {&body}, {&fill}, prefix, cancel, true);
            if(stopped()) return false;
            if(!br.ok) continue;
            const double added = volumeOf(br.shape.shape()) - before;
            const double want = kPiT * (internal ? outer * outer : rMin * rMin - outer * outer) * L;
            if(added < 0.9 * want || added > 1.1 * want + kPiT * 2 * r * grow * L + 1e-9) continue;
            fitted = br.shape;
            filled = true;
            break;
        }
        if(!filled) {
            why = internal ? "the hole could not be fitted to the " + spec.name + " thread"
                           : "the post could not be fitted to the " + spec.name + " thread";
            return false;
        }
    }

    // 1. What the ridge may fill: the full thread along the length; at an open
    // end out through the face (a hole) or down a 45 degree chamfer to the
    // root at the tip (a post); at a closed end fading to the root.
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

    // 2. A hole's countersink at each open face: a cone of the included angle
    // from its diameter at the face (by default a little over the major, so
    // there is no thin, sharp first turn) on past the root into the bore,
    // for a screw to start true.
    TopoDS_Shape cones;
    const bool sinkStart = openStart && csk.atStart, sinkEnd = openEnd && csk.atEnd;
    if(internal && csk.on && (sinkStart || sinkEnd)) {
        const double half = std::clamp(csk.angle, 30.0, 150.0) / 2 * kPiT / 180;
        const double rIn = std::max(0.5 * rMin, rMin - 0.2 * p);
        const double rWant = csk.diameter > 0 ? csk.diameter / 2 : rMaj + 0.15 * p;
        // No deeper than a third of the thread.
        const double rFace = std::min(rWant, rIn + std::tan(half) * L / 3);
        if(rFace > rMin + 1e-6) {
            const double deep = (rFace - rIn) / std::tan(half), over = e * std::tan(half);
            if(sinkStart) cones = revolved({{0, -e}, {rFace + over, -e}, {rIn, deep}, {0, deep}});
            if(sinkEnd) {
                const TopoDS_Shape top = revolved({{0, L - deep}, {rIn, L - deep}, {rFace + over, L + e}, {0, L + e}});
                cones = cones.IsNull() ? top : boolean(Op::Fuse, cones, top, cancel);
            }
            if(stopped()) return false;
        }
    }

    // 3. A hole bored to the root, with its countersinks (shapes of
    // revolution: an easy cut); a post turned down to the root.
    TopoDS_Shape plain = internal ? cylinder(rMin, openStart ? -e : 0, openEnd ? L + e : L)
                                  : boolean(Op::Cut, cylinder(std::max(outer, rMaj) + 1.0, 0, L), cylinder(rMin, 0, L), cancel);
    if(!plain.IsNull() && !cones.IsNull()) plain = boolean(Op::Fuse, plain, cones, cancel);
    if(stopped()) return false;
    const NamedShape plainNamed(placed(plain, frame), {});
    BooleanResult bored = plain.IsNull() ? BooleanResult{} : runBoolean(BoolOp::Cut, {&fitted}, {&plainNamed}, prefix, cancel, true);
    if(stopped()) return false;
    if(!bored.ok) {
        why = internal ? "the hole could not be bored for the " + spec.name + " thread"
                       : "the post could not be turned down for the " + spec.name + " thread";
        return false;
    }
    const NamedShape &turned = bored.shape;

    // 4. The ridge, swept past both ends and trimmed to the envelope, cut into
    // the hole's wall or joined onto the post. A boolean that silently leaves
    // the shape as it was (or does something else) is tried again with the
    // ridge started elsewhere round the axis.
    // A boolean can silently leave the part as it was, or go only partly
    // right (a stretch of the bore left standing): look at the result. Round
    // the middle of the thread, clear of its ends: inside the root all air (a
    // hole) or all part (a post), out past the crests the other way round, and
    // between them the thread's grooves, both. (Measuring the volume of all
    // those helical faces would take seconds.)
    gp_Trsf toPart;
    toPart.SetDisplacement(gp_Ax3(), frame);
    auto looksRight = [&](const TopoDS_Shape &shape) {
        BRepClass3d_SolidClassifier where(shape);
        const double depth = rMaj - rMin;
        auto solidRound = [&](double r, double z) {
            int n = 0;
            for(int k = 0; k < 8; ++k) {
                const double a = (k + 0.5) * kPiT / 4;
                where.Perform(gp_Pnt(r * std::cos(a), r * std::sin(a), z).Transformed(toPart), 1e-6);
                n += where.State() == TopAbs_IN;
            }
            return n;
        };
        const double z = L / 2;
        const int in = solidRound(rMin - depth / 4, z), out = solidRound(rMaj + depth / 4, z), mid = solidRound((rMin + rMaj) / 2, z);
        return (internal ? in == 0 && out == 8 : in == 8 && out == 0) && mid > 0 && mid < 8;
    };
    for(int attempt = 0; attempt < 4; ++attempt) {
        if(stopped()) return false;
        // How far the flanks run on under the root (adjacent turns must not
        // meet: under 0.21 P).
        static constexpr double kSink[] = {0.18, 0.15, 0.12, 0.16};
        // Where round the axis the ridge starts, and how long its pieces are
        // (a retry changes both).
        static constexpr double kTurn[] = {2.6, 0.7, 4.5, 1.6};
        static constexpr double kPerPiece[] = {2, 1.5, 3, 1};
        const double sink = p * kSink[attempt];
        const std::vector<RidgePiece> swept = ridge(p, rMin, rMaj, -2 * p, L + 2 * p, leftHand, sink, kTurn[attempt], kPerPiece[attempt]);
        if(stopped()) return false;
        if(swept.empty()) continue;
        // Trimmed to the envelope where it ends or fades (the pieces between
        // need nothing), and dropped past it.
        const double envLo = internal && openStart ? -past : 0, envHi = internal && openEnd ? L + past : L;
        const double safeLo = openStart ? (internal ? -past : rBig - rMin) : fade;
        const double safeHi = openEnd ? (internal ? L + past : L - (rBig - rMin)) : L - fade;
        std::vector<NamedShape> pieces;
        bool bad = false;
        for(const RidgePiece &piece : swept) {
            if(piece.zHi <= envLo || piece.zLo >= envHi) continue;
            TopoDS_Shape shape = piece.shape;
            if(piece.zLo < safeLo || piece.zHi > safeHi) shape = boolean(Op::Common, shape, envelope, cancel);
            if(stopped()) return false;
            if(shape.IsNull()) {
                bad = true;
                break;
            }
            pieces.emplace_back(placed(shape, frame), std::vector<FaceLabel>{});
        }
        if(bad || pieces.empty()) continue;
        std::vector<const NamedShape *> tools;
        for(const NamedShape &piece : pieces) tools.push_back(&piece);
        const BooleanResult done = runBoolean(internal ? BoolOp::Cut : BoolOp::Fuse, {&turned}, tools, prefix, cancel, true);
        if(stopped()) return false;
        if(!done.ok) continue;
        if(!looksRight(done.shape.shape())) continue;
        result = done.shape;
        return true;
    }
    why = "the " + spec.name + " thread could not be built";
    return false;
}

const char *toString(CountersinkEnds e) {
    return e == CountersinkEnds::Bottom ? "bottom" : e == CountersinkEnds::Both ? "both" : "top";
}

CountersinkEnds countersinkEndsFromString(const std::string &s) {
    return s == "bottom" ? CountersinkEnds::Bottom : s == "both" ? CountersinkEnds::Both : CountersinkEnds::Top;
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
           {"countersinkEnds", toString(countersinkEnds)}, {"countersinkAngle", countersinkAngle}};
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
    countersinkEnds = countersinkEndsFromString(jget<std::string>(j, "countersinkEnds", "top"));
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
        const bool top = countersinkEnds != CountersinkEnds::Bottom, bottom = countersinkEnds != CountersinkEnds::Top;
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
            // The top is the higher end (the one the axis points to, lying flat).
            const bool endIsTop = dir.Z() > -1e-6;
            csk.atEnd = endIsTop ? top : bottom;
            csk.atStart = endIsTop ? bottom : top;
            const std::string tprefix = prefix + "/t" + std::to_string(i + 1);
            // The hole or post is fitted to the thread, then threaded.
            NamedShape result;
            std::string why;
            if(!threadBody(body->shape, *spec, frame, b - a, internal, clear, leftHand, radius, tprefix, result, why,
                           startOpen, endOpen, csk, ctx.cancel))
                return {input, Status::error(why)};
            if(!validateResult(result, prefix, st)) return {input, st};
            replaceBody(*out, body->id, result);
        }
        return {out, st};
    });
}

} // namespace cad
