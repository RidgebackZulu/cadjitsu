#include "features/ShellFeature.h"

#include "features/BodyOps.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRepTools_History.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <map>

namespace cad {

namespace {

// The body offset by `offset` (negative: inwards) as a named solid.
bool offsetSolid(const Body &body, double offset, const std::string &prefix, NamedShape &out, Status &st) {
    BRepOffsetAPI_MakeOffsetShape mk;
    mk.PerformByJoin(body.shape.shape(), offset, 1e-4, BRepOffset_Skin, Standard_False, Standard_False,
                     GeomAbs_Intersection);
    if(!mk.IsDone() || mk.Shape().IsNull()) {
        st.merge(Status::error("cannot offset " + body.name + " for the shell"));
        return false;
    }
    TopoDS_Shape shape = mk.Shape();
    if(solidsOf(shape).empty()) {
        BRepBuilderAPI_MakeSolid ms;
        for(TopExp_Explorer ex(shape, TopAbs_SHELL); ex.More(); ex.Next()) ms.Add(TopoDS::Shell(ex.Current()));
        if(!ms.IsDone()) {
            st.merge(Status::error("cannot offset " + body.name + " for the shell"));
            return false;
        }
        shape = ms.Solid();
        BRepLib::OrientClosedSolid(TopoDS::Solid(shape));
    }
    TopTools_IndexedMapOfShape fmap;
    TopExp::MapShapes(shape, TopAbs_FACE, fmap);
    std::vector<FaceLabel> labels(size_t(fmap.Extent()));
    for(size_t k = 0; k < labels.size(); ++k) labels[k].name = prefix + "/inner/" + std::to_string(k + 1);
    out = NamedShape(shape, std::move(labels));
    return true;
}

bool sealedShell(const Body &body, double offset, const std::string &prefix, NamedShape &out, Status &st) {
    NamedShape other;
    if(!offsetSolid(body, offset, prefix, other, st)) return false;
    const double before = volumeOf(body.shape.shape()), skin = volumeOf(other.shape());
    if(offset < 0 ? skin <= 1e-9 || skin >= before : skin <= before) {
        st.merge(Status::error("the walls are too thick to shell " + body.name));
        return false;
    }
    BooleanResult br = offset < 0 ? runBoolean(BoolOp::Cut, {&body.shape}, {&other}, prefix)
                                  : runBoolean(BoolOp::Cut, {&other}, {&body.shape}, prefix);
    if(!br.ok) {
        st.merge(Status::error(br.error));
        return false;
    }
    out = br.shape;
    return validateResult(out, prefix, st);
}

} // namespace

std::vector<ParamDef> ShellFeature::params() const {
    std::vector<ParamDef> out;
    if(!thickness.empty()) out.push_back({thickness.name, thickness.expr, ValueKind::Length, id, name + " Thickness"});
    return out;
}

json ShellFeature::dataToJson() const {
    json jf = json::array();
    for(const auto &f : faces) jf.push_back(f.toJson());
    return {{"faces", jf},
            {"bodies", bodies},
            {"thickness", thickness.toJson()},
            {"direction", direction == ShellDirection::Inside ? "inside" : "outside"}};
}

void ShellFeature::dataFromJson(const json &j) {
    faces.clear();
    for(const auto &f : j.value("faces", json::array())) faces.push_back(TopoRef::fromJson(f));
    bodies = jget<std::vector<std::string>>(j, "bodies", {});
    thickness = ParamSlot::fromJson(j.value("thickness", json()));
    direction = jget<std::string>(j, "direction", "inside") == "outside" ? ShellDirection::Outside : ShellDirection::Inside;
}

FeatureResult ShellFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        double t = 0.0;
        if(!ctx.value(thickness, t, st)) return {input, st};
        if(!(t > 0)) return {input, Status::error("the shell thickness must be more than 0")};

        // The faces to remove, by body; bodies picked whole get a closed void.
        std::map<BodyId, TopTools_ListOfShape> work;
        for(const auto &fr : faces) {
            ResolvedRef r = resolveRef(*input, fr);
            st.merge(r.status);
            if(!r.ok) continue;
            work[r.body->id].Append(TopoDS::Face(r.shape));
        }
        for(const auto &b : bodies) {
            const BodyId real = input->resolveBodyId(b);
            if(real.empty()) st.merge(Status::warning("body " + b + " no longer exists"));
            else work[real]; // no faces removed
        }
        if(st.isError()) return {input, st};
        if(work.empty()) return {input, Status::error("select the faces to remove (or a body to hollow)")};

        const std::string prefix = "f" + std::to_string(id) + "/shell";
        const double offset = direction == ShellDirection::Inside ? -t : t;
        auto out = std::make_shared<ModelState>(*input);
        for(const auto &[bodyId, removed] : work) {
            const Body *body = input->body(bodyId);
            if(removed.IsEmpty()) {
                // No opening: cut a sealed void (Inside), or wrap the body in
                // a skin and hollow out the body itself (Outside).
                NamedShape result;
                if(!sealedShell(*body, offset, prefix, result, st)) return {input, st};
                replaceBody(*out, bodyId, result);
                continue;
            }
            BRepOffsetAPI_MakeThickSolid mk;
            mk.MakeThickSolidByJoin(body->shape.shape(), removed, offset, 1e-4, BRepOffset_Skin, Standard_False,
                                    Standard_False, GeomAbs_Intersection);
            mk.Build();
            if(!mk.IsDone() || mk.Shape().IsNull())
                return {input, Status::error("cannot shell " + body->name + " with walls " + std::to_string(t).substr(0, 5) +
                                             " mm thick (too thick for the shape, or faces it cannot offset)")};
            TopTools_ListOfShape args;
            args.Append(body->shape.shape());
            Handle(BRepTools_History) hist = new BRepTools_History(args, mk);
            NamedShape result = propagateNames(mk.Shape(), {&body->shape}, hist, prefix);
            if(!validateResult(result, prefix, st)) return {input, st};
            // A wall too thick folds the offset over itself: less volume
            // should be left, never more (Inside) or less than before (Outside).
            const double before = volumeOf(body->shape.shape()), after = volumeOf(result.shape());
            if((direction == ShellDirection::Inside && after >= before - 1e-9) || after <= 1e-9)
                return {input, Status::error("the walls are too thick to shell " + body->name)};
            replaceBody(*out, bodyId, result);
        }
        if(t < kThinWall - 1e-9)
            st.merge(Status::warning("walls thinner than 0.8 mm (two extrusion lines) print poorly"));
        return {out, st};
    });
}

} // namespace cad
