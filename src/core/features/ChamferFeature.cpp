#include "features/ChamferFeature.h"

#include "base/Vec2.h"

#include "features/BodyOps.h"
#include "features/FilletFeature.h"

#include <BRepFilletAPI_MakeChamfer.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>

#include <cmath>

namespace cad {

namespace {

const char *chamferTypeName(ChamferType t) {
    switch(t) {
    case ChamferType::EqualDistance: return "equalDistance";
    case ChamferType::TwoDistances: return "twoDistances";
    case ChamferType::DistanceAngle: return "distanceAngle";
    }
    return "equalDistance";
}

ChamferType chamferTypeFromName(const std::string &s) {
    if(s == "twoDistances") return ChamferType::TwoDistances;
    if(s == "distanceAngle") return ChamferType::DistanceAngle;
    return ChamferType::EqualDistance;
}

} // namespace

std::vector<ParamDef> ChamferFeature::params() const {
    std::vector<ParamDef> out;
    if(!distance.empty()) out.push_back({distance.name, distance.expr, ValueKind::Length, id, name + " Distance"});
    if(!distance2.empty())
        out.push_back({distance2.name, distance2.expr, ValueKind::Length, id, name + " Distance 2"});
    if(!angle.empty()) out.push_back({angle.name, angle.expr, ValueKind::Angle, id, name + " Angle"});
    return out;
}

json ChamferFeature::dataToJson() const {
    json je = json::array(), jf = json::array();
    for(const auto &e : edges) je.push_back(e.toJson());
    for(const auto &f : faces) jf.push_back(f.toJson());
    return json{{"edges", je},
                {"faces", jf},
                {"chamferType", chamferTypeName(chamferType)},
                {"distance", distance.toJson()},
                {"distance2", distance2.toJson()},
                {"angle", angle.toJson()},
                {"flip", flip}};
}

void ChamferFeature::dataFromJson(const json &j) {
    edges.clear();
    faces.clear();
    for(const auto &e : j.value("edges", json::array())) edges.push_back(TopoRef::fromJson(e));
    for(const auto &f : j.value("faces", json::array())) faces.push_back(TopoRef::fromJson(f));
    chamferType = chamferTypeFromName(jget<std::string>(j, "chamferType", ""));
    distance = ParamSlot::fromJson(j.value("distance", json()));
    distance2 = ParamSlot::fromJson(j.value("distance2", json()));
    angle = ParamSlot::fromJson(j.value("angle", json()));
    flip = jget<bool>(j, "flip", false);
}

FeatureResult ChamferFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        double d1 = 0.0, d2 = 0.0, a = 0.0;
        if(!ctx.value(distance, d1, st)) return {input, st};
        if(chamferType == ChamferType::TwoDistances && !ctx.value(distance2, d2, st)) return {input, st};
        if(chamferType == ChamferType::DistanceAngle) {
            if(!ctx.value(angle, a, st, ValueKind::Angle)) return {input, st};
            if(a <= 0.0 || a >= kPi / 2)
                return {input, Status::error("the chamfer angle must be between 0 and 90 degrees")};
        }
        if(d1 <= 0.0 || (chamferType == ChamferType::TwoDistances && d2 <= 0.0))
            return {input, Status::error("chamfer distances must be positive")};

        EdgeSelection sel = resolveEdgeSelection(*input, edges, faces, st);
        if(st.isError()) return {input, st};
        if(sel.byBody.empty()) return {input, Status::error("select edges or faces to chamfer")};

        const std::string prefix = "f" + std::to_string(id) + "/chamfer";
        auto out = std::make_shared<ModelState>(*input);
        for(const auto &[bodyId, list] : sel.byBody) {
            const Body *body = input->body(bodyId);
            TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
            TopExp::MapShapesAndAncestors(body->shape.shape(), TopAbs_EDGE, TopAbs_FACE, edgeFaces);
            BRepFilletAPI_MakeChamfer mk(body->shape.shape());
            for(const auto &e : list) {
                if(chamferType == ChamferType::EqualDistance) {
                    mk.Add(d1, e);
                    continue;
                }
                // Reference face: the adjacent face with the smaller name (stable), or the other one.
                const TopTools_ListOfShape &adj = edgeFaces.FindFromKey(e);
                std::vector<TopoDS_Face> fs;
                for(TopTools_ListOfShape::Iterator it(adj); it.More(); it.Next()) fs.push_back(TopoDS::Face(it.Value()));
                if(fs.size() < 2) continue;
                std::sort(fs.begin(), fs.end(), [&](const TopoDS_Face &x, const TopoDS_Face &y) {
                    return body->shape.faceName(body->shape.indexOf(TopoKind::Face, x)) <
                           body->shape.faceName(body->shape.indexOf(TopoKind::Face, y));
                });
                const TopoDS_Face ref = flip ? fs[1] : fs[0];
                if(chamferType == ChamferType::TwoDistances) mk.Add(d1, d2, e, ref);
                else mk.AddDA(d1, a, e, ref);
            }
            mk.Build();
            if(!mk.IsDone()) return {input, Status::error("the chamfer could not be built (is the distance too large?)")};
            TopTools_ListOfShape args;
            args.Append(body->shape.shape());
            Handle(BRepTools_History) h = new BRepTools_History(args, mk);
            NamedShape result = propagateNames(mk.Shape(), {&body->shape}, h, prefix);
            if(!validateResult(result, prefix, st)) return {input, st};
            replaceBody(*out, bodyId, result);
        }
        return {out, st};
    });
}

} // namespace cad
