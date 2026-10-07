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
#include <TopTools_DataMapOfShapeInteger.hxx>
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
    // Sketch-solved coordinates carry solver error (1e-7..1e-5 mm), so a body
    // drawn onto an existing face or edge can miss it by a hair and leave
    // sliver faces and micro-steps that break meshing. Anything closer than
    // 0.1 um - far below what a printer can make - counts as touching.
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


namespace {

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

FeatureResult applyBodyOperation(const StatePtr &input, const NamedShape &tool, BodyOperation operation,
                                 const std::vector<BodyId> &participants, FeatureId id, const std::string &prefix,
                                 Status st) {
    // 4. Apply the operation. Cuts and intersections show their tool while
    // previewed (also when they fail).
    const std::shared_ptr<const Body> shown =
        operation == BodyOperation::Cut || operation == BodyOperation::Intersect ? toolBody(tool) : nullptr;
    auto fail = [&](Status s) { return FeatureResult{input, std::move(s), shown}; };
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
            return fail(Status::error(operation == BodyOperation::Cut ? "there is no body to cut"
                                                                      : "there is no body to intersect"));
        const BoolOp op = operation == BodyOperation::Cut ? BoolOp::Cut : BoolOp::Common;
        for(const auto &p : parts) {
            const Body *body = input->body(p);
            BooleanResult br = runBoolean(op, {&body->shape}, {&tool}, prefix);
            if(!br.ok) return fail(Status::error(br.error));
            if(solidsOf(br.shape.shape()).empty()) {
                out->bodies.erase(p);
                st.merge(Status::warning(body->name + " was removed entirely"));
                continue;
            }
            if(!validateResult(br.shape, prefix, st)) return fail(st);
            replaceBody(*out, p, br.shape);
        }
        break;
    }
    }
    return {out, st, shown};
}

} // namespace cad
