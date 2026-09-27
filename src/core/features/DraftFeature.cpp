#include "features/DraftFeature.h"

#include "features/BodyOps.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepTools_History.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>

#include <cmath>

namespace cad {

namespace {

// A planar face's outward normal (planeOfFace accounts for the orientation).
std::optional<gp_Dir> outwardNormal(const TopoDS_Face &f) {
    gp_Pln p;
    if(!planeOfFace(f, p)) return std::nullopt;
    return p.Axis().Direction();
}

} // namespace

std::optional<DraftFrame> draftFrame(const TopoDS_Shape &body, const TopoDS_Face &face, const TopoDS_Edge &hinge,
                                     std::string *why) {
    auto fail = [&](const char *m) -> std::optional<DraftFrame> {
        if(why) *why = m;
        return std::nullopt;
    };
    BRepAdaptor_Curve c(hinge);
    if(c.GetType() != GeomAbs_Line) return fail("the hinge must be a straight edge");
    const std::optional<gp_Dir> nFace = outwardNormal(face);
    if(!nFace) return fail("only flat faces can be drafted");
    // The face across the hinge.
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(body, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    const int k = edgeFaces.FindIndex(hinge);
    if(k == 0) return fail("the hinge edge is not on the body");
    std::optional<gp_Dir> nOther;
    bool onFace = false;
    for(const TopoDS_Shape &s : edgeFaces(k)) {
        if(s.IsSame(face)) onFace = true;
        else if(const auto n = outwardNormal(TopoDS::Face(s))) nOther = n;
    }
    if(!onFace) return fail("the hinge must be an edge of the face");
    if(!nOther) return fail("the face across the hinge must be flat (it sets the pull direction)");
    DraftFrame d;
    d.pull = nOther->Reversed();
    const gp_Pnt a = c.Value(c.FirstParameter()), b = c.Value(c.LastParameter());
    d.middle = gp_Pnt((a.XYZ() + b.XYZ()) / 2);
    d.neutral = gp_Pln(d.middle, d.pull);
    const gp_Dir along = c.Line().Direction();
    // In the face, square to the hinge, away from the across face.
    gp_Dir up = nFace->Crossed(along);
    if(up.Dot(d.pull) < 0) up.Reverse();
    d.up = up;
    // A positive turn about the axis takes `up` towards the inside (-nFace).
    gp_Dir axis = along;
    if(axis.Crossed(up).Dot(*nFace) > 0) axis.Reverse();
    d.hingeAxis = gp_Ax1(d.middle, axis);
    return d;
}

std::vector<ParamDef> DraftFeature::params() const {
    if(angle.empty()) return {};
    return {{angle.name, angle.expr, ValueKind::Angle, id, name + " Angle"}};
}

json DraftFeature::dataToJson() const {
    json fs = json::array();
    for(const auto &f : faces) fs.push_back(f.toJson());
    return json{{"faces", fs}, {"hinge", hinge.toJson()}, {"angle", angle.toJson()}, {"flip", flip}};
}

void DraftFeature::dataFromJson(const json &j) {
    faces.clear();
    for(const json &f : j.value("faces", json::array())) faces.push_back(TopoRef::fromJson(f));
    hinge = TopoRef::fromJson(j.value("hinge", json()));
    angle = ParamSlot::fromJson(j.value("angle", json()));
    flip = jget<bool>(j, "flip", false);
}

FeatureResult DraftFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        double a = 0.0;
        if(!ctx.value(angle, a, st, ValueKind::Angle)) return {input, st};
        if(faces.empty()) return {input, Status::error("select the faces to draft")};
        if(std::fabs(a) >= 89.0 * M_PI / 180.0) return {input, Status::error("the draft angle must be under 89 degrees")};
        const ResolvedRef h = resolveRef(*input, hinge);
        st.merge(h.status);
        if(!h.ok) return {input, st};
        std::vector<TopoDS_Face> fs;
        BodyId bodyId;
        for(const TopoRef &f : faces) {
            const ResolvedRef r = resolveRef(*input, f);
            st.merge(r.status);
            if(!r.ok) return {input, st};
            if(bodyId.empty()) bodyId = r.body->id;
            if(r.body->id != bodyId) return {input, Status::error("the drafted faces must be on one body")};
            fs.push_back(TopoDS::Face(r.shape));
        }
        if(h.body->id != bodyId) return {input, Status::error("the hinge must be on the drafted body")};
        const Body *body = h.body;
        // The hinge sets one neutral plane for every face; it must be on one of them.
        std::optional<DraftFrame> frame;
        std::string why;
        for(const TopoDS_Face &f : fs)
            if((frame = draftFrame(body->shape.shape(), f, TopoDS::Edge(h.shape), &why))) break;
        if(!frame) return {input, Status::error(why)};
        if(std::fabs(a) < 1e-12) return {input, st};

        // With the pull away from the hinge's face, BRepOffsetAPI_DraftAngle
        // leans faces in for a positive angle, as ours do.
        const double occt = flip ? -a : a;
        BRepOffsetAPI_DraftAngle draft(body->shape.shape());
        for(size_t i = 0; i < fs.size(); ++i) {
            draft.Add(fs[i], frame->pull, occt, frame->neutral);
            if(!draft.AddDone()) {
                draft.Remove(fs[i]);
                return {input, Status::error("face " + std::to_string(i + 1) +
                                             " cannot be drafted about that hinge (curved neighbours or too steep)")};
            }
        }
        draft.Build();
        if(!draft.IsDone()) return {input, Status::error("the draft could not be built")};
        const std::string prefix = "f" + std::to_string(id) + "/draft";
        TopTools_ListOfShape args;
        args.Append(body->shape.shape());
        Handle(BRepTools_History) hist = new BRepTools_History(args, draft);
        NamedShape result = propagateNames(draft.Shape(), {&body->shape}, hist, prefix);
        if(!validateResult(result, prefix, st)) return {input, st};
        auto out = std::make_shared<ModelState>(*input);
        replaceBody(*out, bodyId, result);
        return {out, st};
    });
}

} // namespace cad
