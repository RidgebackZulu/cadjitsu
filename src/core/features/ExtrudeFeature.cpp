#include "features/ExtrudeFeature.h"

#include "features/BodyOps.h"
#include "geom/OcctUtil.h"
#include "sketch/ProfileToFace.h"
#include "topo/Resolver.h"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_DataMapOfShapeInteger.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

const char *directionName(ExtrudeDirection d) {
    switch(d) {
    case ExtrudeDirection::OneSide: return "oneSide";
    case ExtrudeDirection::TwoSides: return "twoSides";
    case ExtrudeDirection::Symmetric: return "symmetric";
    }
    return "oneSide";
}

ExtrudeDirection directionFromName(const std::string &s) {
    if(s == "twoSides") return ExtrudeDirection::TwoSides;
    if(s == "symmetric") return ExtrudeDirection::Symmetric;
    return ExtrudeDirection::OneSide;
}

const char *extentName(ExtentType e) {
    switch(e) {
    case ExtentType::Distance: return "distance";
    case ExtentType::ThroughAll: return "throughAll";
    case ExtentType::ToObject: return "toObject";
    }
    return "distance";
}

ExtentType extentFromName(const std::string &s) {
    if(s == "throughAll") return ExtentType::ThroughAll;
    if(s == "toObject") return ExtentType::ToObject;
    return ExtentType::Distance;
}

struct PrismInput {
    TopoDS_Face face;
    std::vector<std::pair<TopoDS_Edge, std::string>> edgeNames;
    std::string capKey;
    gp_Dir normal;
};

PrismInput translated(const PrismInput &in, const gp_Vec &v) {
    gp_Trsf t;
    t.SetTranslation(v);
    BRepBuilderAPI_Transform tr(in.face, t, Standard_True);
    PrismInput out;
    out.face = TopoDS::Face(tr.Shape());
    out.capKey = in.capKey;
    out.normal = in.normal;
    for(const auto &[e, n] : in.edgeNames) out.edgeNames.push_back({TopoDS::Edge(tr.ModifiedShape(e)), n});
    return out;
}

std::string firstKey(const std::string &profileKey) { return profileKey.substr(0, profileKey.find(',')); }

// Joins `tool` into the participant bodies. Each result solid keeps the id of
// the first participant it contains; absorbed bodies are recorded as merged.
bool joinInto(ModelState &out, const ModelState &in, const std::vector<BodyId> &parts, const NamedShape &tool,
              FeatureId fid, const std::string &prefix, Status &status) {
    std::vector<const NamedShape *> args;
    for(const auto &p : parts) args.push_back(&in.body(p)->shape);
    BooleanResult br = runBoolean(BoolOp::Fuse, args, {&tool}, prefix);
    if(!br.ok) {
        status.merge(Status::error(br.error));
        return false;
    }
    if(!validateResult(br.shape, prefix, status)) return false;

    const std::vector<TopoDS_Solid> solids = solidsOf(br.shape.shape());
    TopTools_DataMapOfShapeInteger faceSolid;
    for(size_t k = 0; k < solids.size(); ++k)
        for(TopExp_Explorer ex(solids[k], TopAbs_FACE); ex.More(); ex.Next()) faceSolid.Bind(ex.Current(), int(k));

    auto solidOfBody = [&](const Body *b) {
        for(int i = 1; i <= b->shape.faceCount(); ++i) {
            const TopoDS_Face f = b->shape.face(i);
            if(faceSolid.IsBound(f)) return faceSolid.Find(f);
            if(!br.history.IsNull()) {
                for(TopTools_ListOfShape::Iterator it(br.history->Modified(f)); it.More(); it.Next())
                    if(faceSolid.IsBound(it.Value())) return faceSolid.Find(it.Value());
            }
        }
        return -1;
    };

    std::vector<BodyId> owner(solids.size());
    for(const auto &p : parts) {
        const int k = solidOfBody(in.body(p));
        if(k >= 0 && owner[k].empty()) owner[k] = p;
    }
    // Remove all participants; re-add one body per result solid.
    std::map<BodyId, std::shared_ptr<const Body>> old;
    for(const auto &p : parts) {
        old[p] = out.bodies[p];
        out.bodies.erase(p);
    }
    int extra = 0;
    for(size_t k = 0; k < solids.size(); ++k) {
        auto body = std::make_shared<Body>();
        if(!owner[k].empty()) {
            const auto &prev = old[owner[k]];
            body->id = owner[k];
            body->name = prev->name;
            body->order = prev->order;
            body->createdBy = prev->createdBy;
        } else {
            body->id = bodyIdFor(fid) + (extra ? "." + std::to_string(extra + 1) : "");
            ++extra;
            body->name = "Body" + std::to_string(++out.bodyCounter);
            body->order = ++out.bodyOrder;
            body->createdBy = fid;
        }
        body->shape = br.shape.subShape(solids[k]);
        out.bodies[body->id] = body;
    }
    // Participants that did not keep their own solid were merged.
    const BodyId target = parts.front();
    for(const auto &p : parts) {
        if(out.bodies.count(p)) continue;
        const int k = solidOfBody(in.body(p));
        const BodyId into = (k >= 0 && !owner[k].empty()) ? owner[k] : target;
        if(into != p) out.mergedInto[p] = out.bodies.count(into) ? into : target;
    }
    return true;
}

} // namespace

const char *toString(BodyOperation op) {
    switch(op) {
    case BodyOperation::NewBody: return "newBody";
    case BodyOperation::Join: return "join";
    case BodyOperation::Cut: return "cut";
    case BodyOperation::Intersect: return "intersect";
    }
    return "newBody";
}

BodyOperation bodyOperationFromString(const std::string &s) {
    if(s == "join") return BodyOperation::Join;
    if(s == "cut") return BodyOperation::Cut;
    if(s == "intersect") return BodyOperation::Intersect;
    return BodyOperation::NewBody;
}

std::vector<ParamDef> ExtrudeFeature::params() const {
    std::vector<ParamDef> out;
    auto add = [&](const ParamSlot &s, ValueKind k, const char *label) {
        if(!s.empty()) out.push_back({s.name, s.expr, k, id, name + " " + label});
    };
    add(distance, ValueKind::Length, "Distance");
    add(distance2, ValueKind::Length, "Distance 2");
    add(taper, ValueKind::Angle, "Taper Angle");
    add(taper2, ValueKind::Angle, "Taper Angle 2");
    return out;
}

json ExtrudeFeature::dataToJson() const {
    json j;
    json jp = json::array();
    for(const auto &p : profiles) jp.push_back(p.toJson());
    json jf = json::array();
    for(const auto &f : faces) jf.push_back(f.toJson());
    j["profiles"] = jp;
    j["faces"] = jf;
    j["direction"] = directionName(direction);
    j["extent"] = extentName(extent);
    j["extent2"] = extentName(extent2);
    j["distance"] = distance.toJson();
    j["distance2"] = distance2.toJson();
    j["taper"] = taper.toJson();
    j["taper2"] = taper2.toJson();
    if(!toObject.empty()) j["toObject"] = toObject.toJson();
    if(!toObject2.empty()) j["toObject2"] = toObject2.toJson();
    j["flip"] = flip;
    j["operation"] = toString(operation);
    j["participants"] = participants;
    return j;
}

void ExtrudeFeature::dataFromJson(const json &j) {
    profiles.clear();
    faces.clear();
    for(const auto &p : j.value("profiles", json::array())) profiles.push_back(ProfileRef::fromJson(p));
    for(const auto &f : j.value("faces", json::array())) faces.push_back(TopoRef::fromJson(f));
    direction = directionFromName(jget<std::string>(j, "direction", ""));
    extent = extentFromName(jget<std::string>(j, "extent", ""));
    extent2 = extentFromName(jget<std::string>(j, "extent2", ""));
    distance = ParamSlot::fromJson(j.value("distance", json()));
    distance2 = ParamSlot::fromJson(j.value("distance2", json()));
    taper = ParamSlot::fromJson(j.value("taper", json()));
    taper2 = ParamSlot::fromJson(j.value("taper2", json()));
    toObject = TopoRef::fromJson(j.value("toObject", json()));
    toObject2 = TopoRef::fromJson(j.value("toObject2", json()));
    flip = jget<bool>(j, "flip", false);
    operation = bodyOperationFromString(jget<std::string>(j, "operation", ""));
    participants = jget<std::vector<std::string>>(j, "participants", {});
}

std::vector<FeatureId> ExtrudeFeature::dependencies() const {
    std::vector<FeatureId> out;
    for(const auto &p : profiles)
        if(std::find(out.begin(), out.end(), p.sketch) == out.end()) out.push_back(p.sketch);
    return out;
}

FeatureResult ExtrudeFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        const std::string prefix = "f" + std::to_string(id);

        // 1. Collect the faces to sweep.
        std::vector<PrismInput> inputs;
        for(const auto &pr : profiles) {
            const SketchResult *sk = nullptr;
            const Profile *p = resolveProfile(*input, pr, sk, st);
            if(!p) continue;
            ProfileFace pf;
            std::string err;
            if(!profileToFace(*p, sk->frame, pf, err)) {
                st.merge(Status::error(err));
                continue;
            }
            PrismInput in;
            in.face = pf.face;
            in.normal = sk->frame.Direction();
            const std::string skp = "s" + std::to_string(pr.sketch) + ":";
            for(const auto &[e, k] : pf.edgeKeys) in.edgeNames.push_back({e, skp + k});
            in.capKey = skp + firstKey(p->key);
            inputs.push_back(std::move(in));
        }
        for(const auto &fr : faces) {
            ResolvedRef r = resolveRef(*input, fr);
            st.merge(r.status);
            if(!r.ok) continue;
            const TopoDS_Face f = TopoDS::Face(r.shape);
            gp_Pln pln;
            if(!planeOfFace(f, pln)) {
                st.merge(Status::error("only planar faces can be extruded"));
                continue;
            }
            BRepBuilderAPI_Copy copier(f);
            PrismInput in;
            in.face = TopoDS::Face(copier.Shape());
            in.normal = pln.Axis().Direction();
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
        if(inputs.empty()) return {input, Status::error("nothing to extrude: select a profile or a planar face")};

        // 2. Extent along the normal: [start, end].
        Bnd_Box box;
        for(const auto &kv : input->bodies) box.Add(boundingBox(kv.second->shape.shape()));
        for(const auto &in : inputs) box.Add(boundingBox(in.face));
        const double big = 2.0 * std::sqrt(box.IsVoid() ? 1.0 : box.SquareExtent()) + 10.0;

        auto sideLength = [&](ExtentType e, const ParamSlot &slot, double &v) {
            if(e == ExtentType::ThroughAll) {
                v = big;
                return true;
            }
            if(e == ExtentType::ToObject) {
                st.merge(Status::error("the To Object extent is not available yet"));
                return false;
            }
            return ctx.value(slot, v, st);
        };
        double start = 0.0, end = 0.0, a = 0.0, b = 0.0;
        switch(direction) {
        case ExtrudeDirection::OneSide:
            if(!sideLength(extent, distance, a)) return {input, st};
            end = a;
            break;
        case ExtrudeDirection::Symmetric:
            if(!sideLength(extent, distance, a)) return {input, st};
            start = -std::fabs(a);
            end = std::fabs(a);
            break;
        case ExtrudeDirection::TwoSides:
            if(!sideLength(extent, distance, a) || !sideLength(extent2, distance2, b)) return {input, st};
            start = -b;
            end = a;
            break;
        }
        if(std::fabs(end - start) < 1e-7) return {input, Status::error("the extrude distance is zero")};
        if(!taper.empty()) {
            double t = 0.0;
            if(!ctx.value(taper, t, st, ValueKind::Angle)) return {input, st};
            if(std::fabs(t) > 1e-9) st.merge(Status::warning("taper angles are not supported yet; ignored"));
        }

        // 3. Sweep each face and name the result.
        std::vector<NamedShape> prisms;
        for(const auto &in0 : inputs) {
            const gp_Dir n = flip ? in0.normal.Reversed() : in0.normal;
            const PrismInput in = std::fabs(start) > 0 ? translated(in0, gp_Vec(n) * start) : in0;
            BRepPrimAPI_MakePrism mk(in.face, gp_Vec(n) * (end - start));
            mk.Build();
            if(!mk.IsDone()) return {input, Status::error("could not sweep the profile")};
            const TopoDS_Shape solid = mk.Shape();
            TopTools_IndexedMapOfShape fmap;
            TopExp::MapShapes(solid, TopAbs_FACE, fmap);
            std::vector<FaceLabel> labels(size_t(fmap.Extent()));
            for(const auto &[e, nm] : in.edgeNames) {
                for(TopTools_ListOfShape::Iterator it(mk.Generated(e)); it.More(); it.Next()) {
                    const int j = fmap.FindIndex(it.Value());
                    if(j > 0) labels[j - 1].name = prefix + "/side/" + nm;
                }
            }
            auto setCap = [&](const TopoDS_Shape &s, const char *tag) {
                for(TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
                    const int j = fmap.FindIndex(ex.Current());
                    if(j > 0) labels[j - 1].name = prefix + "/" + tag + "/" + in.capKey;
                }
            };
            setCap(mk.FirstShape(), "start");
            setCap(mk.LastShape(), "end");
            for(auto &l : labels)
                if(l.name.empty()) l.name = prefix + "/side/other";
            prisms.emplace_back(solid, std::move(labels));
        }
        NamedShape tool;
        if(prisms.size() == 1) {
            tool = prisms[0];
        } else {
            std::vector<const NamedShape *> args{&prisms[0]}, tools;
            for(size_t k = 1; k < prisms.size(); ++k) tools.push_back(&prisms[k]);
            BooleanResult br = runBoolean(BoolOp::Fuse, args, tools, prefix);
            if(!br.ok) return {input, Status::error(br.error)};
            tool = br.shape;
        }

        // 4. Apply the operation.
        auto out = std::make_shared<ModelState>(*input);
        std::vector<BodyId> parts;
        if(operation != BodyOperation::NewBody) {
            if(participants.empty()) {
                parts = bodiesInteracting(*input, tool.shape(), operation == BodyOperation::Join);
            } else {
                for(const auto &b : participants) {
                    const BodyId real = input->resolveBodyId(b);
                    if(real.empty()) st.merge(Status::warning("body " + b + " no longer exists"));
                    else if(std::find(parts.begin(), parts.end(), real) == parts.end()) parts.push_back(real);
                }
            }
        }
        switch(operation) {
        case BodyOperation::NewBody:
            addNewBodies(*out, tool, id);
            break;
        case BodyOperation::Join:
            if(parts.empty()) addNewBodies(*out, tool, id);
            else if(!joinInto(*out, *input, parts, tool, id, prefix, st)) return {input, st};
            break;
        case BodyOperation::Cut:
        case BodyOperation::Intersect: {
            if(parts.empty())
                return {input, Status::error(operation == BodyOperation::Cut ? "there is no body to cut"
                                                                             : "there is no body to intersect")};
            const BoolOp op = operation == BodyOperation::Cut ? BoolOp::Cut : BoolOp::Common;
            for(const auto &p : parts) {
                const Body *body = input->body(p);
                BooleanResult br = runBoolean(op, {&body->shape}, {&tool}, prefix);
                if(!br.ok) return {input, Status::error(br.error)};
                if(solidsOf(br.shape.shape()).empty()) {
                    out->bodies.erase(p);
                    st.merge(Status::warning(body->name + " was removed entirely"));
                    continue;
                }
                if(!validateResult(br.shape, prefix, st)) return {input, st};
                replaceBody(*out, p, br.shape);
            }
            break;
        }
        }
        return {out, st};
    });
}

} // namespace cad
