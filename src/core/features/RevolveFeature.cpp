#include "features/RevolveFeature.h"

#include "geom/OcctUtil.h"
#include "sketch/ProfileToFace.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <gp_Pln.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

struct RevolveInput {
    TopoDS_Face face;
    std::vector<std::pair<TopoDS_Edge, std::string>> edgeNames;
    std::string capKey;
};

std::string firstKey(const std::string &profileKey) { return profileKey.substr(0, profileKey.find(',')); }

// Which side of the axis the face lies on (in the face's plane): +1 / -1, 0
// when it lies on both (the axis cuts through it).
int sideOfAxis(const TopoDS_Face &face, const gp_Ax1 &axis, double tol) {
    gp_Pln pln;
    gp_Dir n(0, 0, 1);
    if(planeOfFace(face, pln)) n = pln.Axis().Direction();
    int pos = 0, neg = 0;
    for(TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
        const double t0 = c.FirstParameter(), t1 = c.LastParameter();
        for(int i = 0; i <= 16; ++i) {
            const gp_Pnt p = c.Value(t0 + (t1 - t0) * i / 16.0);
            const double s = gp_Vec(axis.Location(), p).Crossed(gp_Vec(axis.Direction())).Dot(gp_Vec(n));
            if(s > tol) ++pos;
            else if(s < -tol) ++neg;
        }
    }
    if(pos && neg) return 0;
    return neg ? -1 : 1;
}

} // namespace

json RevolveAxis::toJson() const {
    json j;
    if(!axis.empty()) j["axis"] = axis.toJson();
    if(sketch) j["sketchLine"] = {sketch, line};
    return j;
}

RevolveAxis RevolveAxis::fromJson(const json &j) {
    RevolveAxis a;
    if(!j.is_object()) return a;
    if(j.contains("axis")) a.axis = PatternAxis::fromJson(j["axis"]);
    if(j.contains("sketchLine") && j["sketchLine"].is_array() && j["sketchLine"].size() == 2) {
        a.sketch = j["sketchLine"][0].get<FeatureId>();
        a.line = j["sketchLine"][1].get<int>();
    }
    return a;
}

std::optional<gp_Ax1> resolveRevolveAxis(const ModelState &state, const RevolveAxis &a, Status &status) {
    if(!a.sketch) return resolveAxis(state, a.axis, status);
    auto it = state.sketches.find(a.sketch);
    if(it == state.sketches.end() || !it->second) {
        status.merge(Status::error("the sketch with the revolve axis is gone"));
        return std::nullopt;
    }
    const SketchResult &sk = *it->second;
    if(a.line == kSketchXAxis) return gp_Ax1(sk.frame.Location(), sk.frame.XDirection());
    if(a.line == kSketchYAxis) return gp_Ax1(sk.frame.Location(), sk.frame.YDirection());
    const SkEntity *e = sk.sketch.find(a.line);
    if(!e || e->type != SkType::Line) {
        status.merge(Status::error("the revolve axis line is gone from " + sk.name));
        return std::nullopt;
    }
    const gp_Pnt p0 = sk.toWorld(sk.sketch.pointPos(e->a)), p1 = sk.toWorld(sk.sketch.pointPos(e->b));
    if(p0.Distance(p1) < 1e-9) {
        status.merge(Status::error("the revolve axis line has no length"));
        return std::nullopt;
    }
    return gp_Ax1(p0, gp_Dir(gp_Vec(p0, p1)));
}

const char *toString(RevolveExtent e) {
    switch(e) {
    case RevolveExtent::OneSide: return "oneSide";
    case RevolveExtent::Symmetric: return "symmetric";
    case RevolveExtent::TwoSides: return "twoSides";
    }
    return "oneSide";
}

RevolveExtent revolveExtentFromString(const std::string &s) {
    if(s == "symmetric") return RevolveExtent::Symmetric;
    if(s == "twoSides") return RevolveExtent::TwoSides;
    return RevolveExtent::OneSide;
}

std::vector<ParamDef> RevolveFeature::params() const {
    std::vector<ParamDef> out;
    if(!angle.empty()) out.push_back({angle.name, angle.expr, ValueKind::Angle, id, name + " Angle"});
    if(!angle2.empty()) out.push_back({angle2.name, angle2.expr, ValueKind::Angle, id, name + " Angle 2"});
    return out;
}

json RevolveFeature::dataToJson() const {
    json j;
    json jp = json::array(), jf = json::array();
    for(const auto &p : profiles) jp.push_back(p.toJson());
    for(const auto &f : faces) jf.push_back(f.toJson());
    j["profiles"] = jp;
    j["faces"] = jf;
    j["axis"] = axis.toJson();
    j["extent"] = toString(extent);
    j["angle"] = angle.toJson();
    j["angle2"] = angle2.toJson();
    j["flip"] = flip;
    j["operation"] = toString(operation);
    j["participants"] = participants;
    return j;
}

void RevolveFeature::dataFromJson(const json &j) {
    profiles.clear();
    faces.clear();
    for(const auto &p : j.value("profiles", json::array())) profiles.push_back(ProfileRef::fromJson(p));
    for(const auto &f : j.value("faces", json::array())) faces.push_back(TopoRef::fromJson(f));
    axis = RevolveAxis::fromJson(j.value("axis", json()));
    extent = revolveExtentFromString(jget<std::string>(j, "extent", ""));
    angle = ParamSlot::fromJson(j.value("angle", json()));
    angle2 = ParamSlot::fromJson(j.value("angle2", json()));
    flip = jget<bool>(j, "flip", false);
    operation = bodyOperationFromString(jget<std::string>(j, "operation", ""));
    participants = jget<std::vector<std::string>>(j, "participants", {});
}

std::vector<FeatureId> RevolveFeature::dependencies() const {
    std::vector<FeatureId> out;
    for(const auto &p : profiles)
        if(std::find(out.begin(), out.end(), p.sketch) == out.end()) out.push_back(p.sketch);
    if(axis.sketch && std::find(out.begin(), out.end(), axis.sketch) == out.end()) out.push_back(axis.sketch);
    return out;
}

FeatureResult RevolveFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        const std::string prefix = "f" + std::to_string(id);

        // 1. The faces to turn.
        std::vector<RevolveInput> inputs;
        for(const auto &pr : profiles) {
            const SketchResult *sk = nullptr;
            for(const Profile *p : resolveProfiles(*input, pr, sk, st)) {
                ProfileFace pf;
                std::string err;
                if(!profileToFace(*p, sk->frame, pf, err)) {
                    st.merge(Status::error(err));
                    continue;
                }
                RevolveInput in;
                in.face = pf.face;
                const std::string skp = "s" + std::to_string(pr.sketch) + ":";
                for(const auto &[e, k] : pf.edgeKeys) in.edgeNames.push_back({e, skp + k});
                in.capKey = skp + firstKey(p->key);
                inputs.push_back(std::move(in));
            }
        }
        for(const auto &fr : faces) {
            ResolvedRef r = resolveRef(*input, fr);
            st.merge(r.status);
            if(!r.ok) continue;
            const TopoDS_Face f = TopoDS::Face(r.shape);
            gp_Pln pln;
            if(!planeOfFace(f, pln)) {
                st.merge(Status::error("only planar faces can be revolved"));
                continue;
            }
            BRepBuilderAPI_Copy copier(f);
            RevolveInput in;
            in.face = TopoDS::Face(copier.Shape());
            for(TopExp_Explorer ex(f, TopAbs_EDGE); ex.More(); ex.Next()) {
                const TopoDS_Edge e = TopoDS::Edge(ex.Current());
                const int ei = r.body->shape.indexOf(TopoKind::Edge, e);
                in.edgeNames.push_back(
                    {TopoDS::Edge(copier.ModifiedShape(e)), ei ? r.body->shape.edgeName(ei) : std::string("edge")});
            }
            in.capKey = r.body->shape.faceName(r.index);
            inputs.push_back(std::move(in));
        }
        if(st.isError()) return {input, st};
        if(inputs.empty()) return {input, Status::error("nothing to revolve: select a profile or a planar face")};

        // 2. The axis and the angles.
        if(axis.empty()) return {input, Status::error("select the axis to revolve about")};
        const std::optional<gp_Ax1> ax = resolveRevolveAxis(*input, axis, st);
        if(!ax) return {input, st};
        for(const auto &in : inputs) {
            if(sideOfAxis(in.face, *ax, 1e-6) == 0)
                return {input, Status::error("the profile crosses the axis: a revolve needs the whole profile on one "
                                             "side of it (it may touch it)")};
        }
        double a = 2 * kPi, b = 0.0;
        if(!angle.empty() && !ctx.value(angle, a, st, ValueKind::Angle)) return {input, st};
        if(extent == RevolveExtent::TwoSides && !angle2.empty() && !ctx.value(angle2, b, st, ValueKind::Angle))
            return {input, st};
        double start = 0.0, end = a;
        if(extent == RevolveExtent::Symmetric) {
            start = -std::fabs(a);
            end = std::fabs(a);
        } else if(extent == RevolveExtent::TwoSides) {
            start = -b;
        }
        if(flip) {
            std::swap(start, end);
            start = -start;
            end = -end;
        }
        double total = end - start;
        if(std::fabs(total) < 1e-9) return {input, Status::error("the revolve angle is zero")};
        const bool full = std::fabs(total) >= 2 * kPi - 1e-9;
        if(full) {
            start = 0.0;
            total = 2 * kPi;
        }

        // 3. Turn each face and name the result like an extrude's: sides from
        // the profile's edges, start and end faces when not all the way round.
        std::vector<NamedShape> solids;
        for(const RevolveInput &in0 : inputs) {
            RevolveInput in = in0;
            if(std::fabs(start) > 1e-12) {
                gp_Trsf t;
                t.SetRotation(*ax, start);
                BRepBuilderAPI_Transform tr(in0.face, t, Standard_True);
                in.face = TopoDS::Face(tr.Shape());
                in.edgeNames.clear();
                for(const auto &[e, n] : in0.edgeNames) in.edgeNames.push_back({TopoDS::Edge(tr.ModifiedShape(e)), n});
            }
            BRepPrimAPI_MakeRevol mk = full ? BRepPrimAPI_MakeRevol(in.face, *ax, 2 * kPi, Standard_True)
                                            : BRepPrimAPI_MakeRevol(in.face, *ax, total, Standard_True);
            mk.Build();
            if(!mk.IsDone()) return {input, Status::error("could not revolve the profile")};
            const TopoDS_Shape solid = mk.Shape();
            TopTools_IndexedMapOfShape fmap;
            TopExp::MapShapes(solid, TopAbs_FACE, fmap);
            std::vector<FaceLabel> labels(size_t(fmap.Extent()));
            for(const auto &[e, nm] : in.edgeNames) {
                for(TopTools_ListOfShape::Iterator it(mk.Generated(e)); it.More(); it.Next()) {
                    const int j = fmap.FindIndex(it.Value());
                    if(j > 0) labels[size_t(j - 1)].name = prefix + "/side/" + nm;
                }
            }
            if(!full) {
                auto setCap = [&](const TopoDS_Shape &cap, const char *tag) {
                    for(TopExp_Explorer ex(cap, TopAbs_FACE); ex.More(); ex.Next()) {
                        const int j = fmap.FindIndex(ex.Current());
                        if(j > 0) labels[size_t(j - 1)].name = prefix + "/" + tag + "/" + in.capKey;
                    }
                };
                setCap(mk.FirstShape(), "start");
                setCap(mk.LastShape(), "end");
            }
            for(auto &l : labels)
                if(l.name.empty()) l.name = prefix + "/side/other";
            NamedShape named(solid, std::move(labels));
            if(!validateResult(named, prefix, st)) return {input, st};
            solids.push_back(std::move(named));
        }
        NamedShape tool;
        if(solids.size() == 1) {
            tool = solids[0];
        } else {
            std::vector<const NamedShape *> args{&solids[0]}, tools;
            for(size_t k = 1; k < solids.size(); ++k) tools.push_back(&solids[k]);
            BooleanResult br = runBoolean(BoolOp::Fuse, args, tools, prefix);
            if(!br.ok) return {input, Status::error(br.error)};
            tool = br.shape;
        }

        // 4. New body, join, cut or intersect.
        return applyBodyOperation(input, tool, operation, participants, id, prefix, std::move(st));
    });
}

} // namespace cad
