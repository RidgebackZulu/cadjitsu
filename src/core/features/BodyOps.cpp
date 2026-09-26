#include "features/BodyOps.h"

#include "geom/OcctUtil.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <Message_Report.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>

#include <algorithm>
#include <memory>

namespace cad {

BooleanResult runBoolean(BoolOp op, const std::vector<const NamedShape *> &args,
                         const std::vector<const NamedShape *> &tools, const std::string &prefix) {
    BooleanResult r;
    TopTools_ListOfShape a, t;
    for(const auto *ns : args) a.Append(ns->shape());
    for(const auto *ns : tools) t.Append(ns->shape());

    std::unique_ptr<BRepAlgoAPI_BooleanOperation> algo;
    switch(op) {
    case BoolOp::Fuse: algo = std::make_unique<BRepAlgoAPI_Fuse>(); break;
    case BoolOp::Cut: algo = std::make_unique<BRepAlgoAPI_Cut>(); break;
    case BoolOp::Common: algo = std::make_unique<BRepAlgoAPI_Common>(); break;
    }
    algo->SetArguments(a);
    algo->SetTools(t);
    algo->SetNonDestructive(Standard_True);
    algo->SetRunParallel(Standard_False);
    // Sketch-solved coordinates are good to ~1e-7 mm, right at OCCT's own
    // confusion tolerance, so faces drawn onto existing ones can miss them by
    // a hair and leave sliver faces the mesher cannot handle. Treat anything
    // closer than 1 nm as touching (wider hairlines are handled at export).
    algo->SetFuzzyValue(kBooleanFuzz);
    algo->Build();
    if(algo->HasErrors() || !algo->IsDone()) {
        r.error = "the boolean operation failed";
        return r;
    }
    algo->SimplifyResult();
    r.history = algo->History();
    std::vector<const NamedShape *> inputs = args;
    inputs.insert(inputs.end(), tools.begin(), tools.end());
    r.shape = propagateNames(algo->Shape(), inputs, r.history, prefix);
    r.ok = true;
    return r;
}

NamedShape unifyNamed(const NamedShape &shape, const std::string &prefix) {
    ShapeUpgrade_UnifySameDomain unify(shape.shape(), Standard_True, Standard_True, Standard_False);
    unify.SetSafeInputMode(Standard_True);
    unify.Build();
    return propagateNames(unify.Shape(), {&shape}, unify.History(), prefix);
}

std::shared_ptr<const Body> toolBody(const NamedShape &shape) {
    auto b = std::make_shared<Body>();
    b->id = "tool";
    b->name = "tool";
    b->shape = shape;
    return b;
}

std::vector<BodyId> addNewBodies(ModelState &state, const NamedShape &shape, FeatureId fid) {
    std::vector<TopoDS_Solid> solids = solidsOf(shape.shape());
    std::vector<std::pair<double, TopoDS_Solid>> sized;
    for(const auto &s : solids) sized.push_back({volumeOf(s), s});
    std::stable_sort(sized.begin(), sized.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
    std::vector<BodyId> ids;
    for(size_t k = 0; k < sized.size(); ++k) {
        if(sized[k].first <= 1e-12) continue;
        auto body = std::make_shared<Body>();
        body->id = bodyIdFor(fid) + (k ? "." + std::to_string(k + 1) : "");
        body->name = "Body" + std::to_string(++state.bodyCounter);
        body->order = ++state.bodyOrder;
        body->createdBy = fid;
        body->shape = shape.subShape(sized[k].second);
        ids.push_back(body->id);
        state.bodies[body->id] = std::move(body);
    }
    return ids;
}

std::vector<BodyId> replaceBody(ModelState &state, const BodyId &id, const NamedShape &result) {
    auto it = state.bodies.find(id);
    if(it == state.bodies.end()) return {};
    const std::shared_ptr<const Body> old = it->second;
    state.bodies.erase(it);

    std::vector<std::pair<double, TopoDS_Solid>> sized;
    for(const auto &s : solidsOf(result.shape())) {
        const double v = volumeOf(s);
        if(v > 1e-12) sized.push_back({v, s});
    }
    std::stable_sort(sized.begin(), sized.end(), [](const auto &x, const auto &y) { return x.first > y.first; });
    std::vector<BodyId> ids;
    for(size_t k = 0; k < sized.size(); ++k) {
        auto body = std::make_shared<Body>();
        body->id = k == 0 ? id : id + "." + std::to_string(k + 1);
        body->name = k == 0 ? old->name : "Body" + std::to_string(++state.bodyCounter);
        body->order = k == 0 ? old->order : ++state.bodyOrder;
        body->createdBy = old->createdBy;
        body->shape = result.subShape(sized[k].second);
        ids.push_back(body->id);
        state.bodies[body->id] = std::move(body);
    }
    return ids;
}

bool validateResult(NamedShape &shape, const std::string &prefix, Status &status) {
    if(shape.isNull() || solidsOf(shape.shape()).empty()) {
        status.merge(Status::error("the operation produced no solid"));
        return false;
    }
    if(BRepCheck_Analyzer(shape.shape()).IsValid()) return true;
    ShapeFix_Shape fix(shape.shape());
    fix.Perform();
    const TopoDS_Shape fixed = fix.Shape();
    if(fixed.IsNull() || !BRepCheck_Analyzer(fixed).IsValid()) {
        status.merge(Status::error("the result is not a valid solid"));
        return false;
    }
    // Names survive through the fixer's history.
    Handle(BRepTools_History) h = new BRepTools_History();
    TopTools_IndexedMapOfShape faces;
    for(int i = 1; i <= shape.faceCount(); ++i) {
        const TopoDS_Shape img = fix.Context()->Value(shape.face(i));
        if(!img.IsSame(shape.face(i)) && !img.IsNull()) {
            for(TopExp_Explorer ex(img, TopAbs_FACE); ex.More(); ex.Next()) h->AddModified(shape.face(i), ex.Current());
        }
    }
    // Only tolerances and curves on surface adjusted (typical after faces
    // were merged across the booleans' fuzzy gap): nothing to tell the user.
    auto count = [](const TopoDS_Shape &sh, TopAbs_ShapeEnum kind) {
        TopTools_IndexedMapOfShape m;
        TopExp::MapShapes(sh, kind, m);
        return m.Extent();
    };
    const bool sameTopology = count(fixed, TopAbs_FACE) == count(shape.shape(), TopAbs_FACE) &&
                              count(fixed, TopAbs_EDGE) == count(shape.shape(), TopAbs_EDGE);
    shape = propagateNames(fixed, {&shape}, h, prefix);
    if(!sameTopology) status.merge(Status::warning("the result needed repair"));
    return true;
}

std::vector<BodyId> bodiesInteracting(const ModelState &state, const TopoDS_Shape &tool, bool touching) {
    std::vector<BodyId> out;
    Bnd_Box tb = boundingBox(tool);
    if(tb.IsVoid()) return out;
    tb.Enlarge(1e-4);
    for(const Body *b : state.orderedBodies()) {
        Bnd_Box bb = boundingBox(b->shape.shape());
        if(bb.IsVoid() || bb.IsOut(tb)) continue;
        bool hit = false;
        BRepAlgoAPI_Common common;
        TopTools_ListOfShape args, tools;
        args.Append(b->shape.shape());
        tools.Append(tool);
        common.SetArguments(args);
        common.SetTools(tools);
        common.SetNonDestructive(Standard_True);
        common.Build();
        if(common.IsDone() && !common.HasErrors() && volumeOf(common.Shape()) > 1e-9) hit = true;
        if(!hit && touching) {
            BRepExtrema_DistShapeShape dist(b->shape.shape(), tool);
            if(dist.IsDone() && dist.Value() < 1e-6) hit = true;
        }
        if(hit) out.push_back(b->id);
    }
    return out;
}

} // namespace cad
