#include "features/FilletFeature.h"

#include "features/BodyOps.h"
#include "topo/Resolver.h"

#include <BRepFilletAPI_MakeFillet.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

namespace cad {

EdgeSelection resolveEdgeSelection(const ModelState &state, const std::vector<TopoRef> &edges,
                                   const std::vector<TopoRef> &faces, Status &status) {
    EdgeSelection sel;
    std::map<BodyId, TopTools_IndexedMapOfShape> seen;
    auto add = [&](const Body *body, const TopoDS_Edge &e) {
        const int idx = body->shape.indexOf(TopoKind::Edge, e);
        if(!body->shape.isSelectableEdge(idx)) return;
        TopTools_IndexedMapOfShape &m = seen[body->id];
        if(m.Contains(e)) return;
        m.Add(e);
        sel.byBody[body->id].push_back(e);
    };
    for(const auto &ref : edges) {
        ResolvedRef r = resolveRef(state, ref);
        status.merge(r.status);
        if(r.ok) add(r.body, TopoDS::Edge(r.shape));
    }
    for(const auto &ref : faces) {
        ResolvedRef r = resolveRef(state, ref);
        status.merge(r.status);
        if(!r.ok) continue;
        for(TopExp_Explorer ex(r.shape, TopAbs_EDGE); ex.More(); ex.Next()) add(r.body, TopoDS::Edge(ex.Current()));
    }
    return sel;
}

std::vector<ParamDef> FilletFeature::params() const {
    if(radius.empty()) return {};
    return {{radius.name, radius.expr, ValueKind::Length, id, name + " Radius"}};
}

json FilletFeature::dataToJson() const {
    json je = json::array(), jf = json::array();
    for(const auto &e : edges) je.push_back(e.toJson());
    for(const auto &f : faces) jf.push_back(f.toJson());
    return json{{"edges", je}, {"faces", jf}, {"radius", radius.toJson()}};
}

void FilletFeature::dataFromJson(const json &j) {
    edges.clear();
    faces.clear();
    for(const auto &e : j.value("edges", json::array())) edges.push_back(TopoRef::fromJson(e));
    for(const auto &f : j.value("faces", json::array())) faces.push_back(TopoRef::fromJson(f));
    radius = ParamSlot::fromJson(j.value("radius", json()));
}

FeatureResult FilletFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        double r = 0.0;
        if(!ctx.value(radius, r, st)) return {input, st};
        if(r <= 0.0) return {input, Status::error("the fillet radius must be positive")};

        EdgeSelection sel = resolveEdgeSelection(*input, edges, faces, st);
        if(st.isError()) return {input, st};
        if(sel.byBody.empty()) return {input, Status::error("select edges or faces to fillet")};

        const std::string prefix = "f" + std::to_string(id) + "/fillet";
        auto out = std::make_shared<ModelState>(*input);
        for(const auto &[bodyId, list] : sel.byBody) {
            const Body *body = input->body(bodyId);
            BRepFilletAPI_MakeFillet mk(body->shape.shape());
            for(const auto &e : list) mk.Add(r, e);
            mk.Build();
            if(!mk.IsDone())
                return {input, Status::error("the fillet could not be built (is the radius too large?)")};
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
