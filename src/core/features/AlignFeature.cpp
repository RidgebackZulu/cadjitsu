#include "features/AlignFeature.h"

#include "features/BodyOps.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>

namespace cad {

namespace {

// `src` moved by `t`, its faces named `prefix` + their old names (an empty
// prefix: the same names, so references to them still hold).
NamedShape movedNamed(const NamedShape &src, const gp_Trsf &t, const std::string &prefix) {
    BRepBuilderAPI_Transform tr(src.shape(), t, Standard_True);
    const TopoDS_Shape out = tr.Shape();
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(out, TopAbs_FACE, faces);
    std::vector<FaceLabel> labels(size_t(faces.Extent()));
    for(int i = 1; i <= src.faceCount(); ++i) {
        const int j = faces.FindIndex(tr.ModifiedShape(src.face(i)));
        if(j > 0) labels[size_t(j - 1)].name = prefix + src.faceName(i);
    }
    for(size_t j = 0; j < labels.size(); ++j)
        if(labels[j].name.empty()) labels[j].name = prefix + "moved/" + std::to_string(j + 1);
    return NamedShape(out, std::move(labels));
}

} // namespace

json AlignRef::toJson() const {
    json j;
    if(!builtin.empty()) j["builtin"] = builtin;
    if(!ref.empty()) j["ref"] = ref.toJson();
    if(plane) j["plane"] = plane->toJson();
    if(at) j["at"] = {at->X(), at->Y(), at->Z()};
    return j;
}

AlignRef AlignRef::fromJson(const json &j) {
    AlignRef r;
    if(!j.is_object()) return r;
    r.builtin = jget<std::string>(j, "builtin", "");
    if(j.contains("ref")) r.ref = TopoRef::fromJson(j["ref"]);
    if(j.contains("plane")) r.plane = PlaneRef::fromJson(j["plane"]);
    if(j.contains("at") && j["at"].is_array() && j["at"].size() == 3)
        r.at = gp_Pnt(j["at"][0].get<double>(), j["at"][1].get<double>(), j["at"][2].get<double>());
    return r;
}

std::optional<AlignAnchor> anchorOf(const ModelState &state, const AlignRef &r, Status &status) {
    AlignAnchor a;
    const gp_Pnt o(0, 0, 0);
    if(!r.builtin.empty()) {
        const std::string &b = r.builtin;
        if(b == "origin") return AlignAnchor{AlignKind::Point, o, gp_Dir(0, 0, 1), true};
        if(b == "x" || b == "y" || b == "z")
            return AlignAnchor{AlignKind::Line, o, b == "x" ? gp_Dir(1, 0, 0) : b == "y" ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1), false};
        if(b == "xy" || b == "xz" || b == "yz")
            return AlignAnchor{AlignKind::Plane, o, b == "xy" ? gp_Dir(0, 0, 1) : b == "xz" ? gp_Dir(0, 1, 0) : gp_Dir(1, 0, 0), true};
        status.merge(Status::error("unknown origin entity " + b));
        return std::nullopt;
    }
    if(r.plane) {
        gp_Ax3 frame;
        if(!resolvePlane(state, *r.plane, frame, status)) return std::nullopt;
        return AlignAnchor{AlignKind::Plane, frame.Location(), frame.Direction(), true};
    }
    if(r.ref.empty()) {
        status.merge(Status::error("pick what to align"));
        return std::nullopt;
    }
    const ResolvedRef rr = resolveRef(state, r.ref);
    status.merge(rr.status);
    if(!rr.ok) return std::nullopt;
    switch(rr.shape.ShapeType()) {
    case TopAbs_VERTEX:
        return AlignAnchor{AlignKind::Point, BRep_Tool::Pnt(TopoDS::Vertex(rr.shape)), gp_Dir(0, 0, 1), true};
    case TopAbs_EDGE: {
        BRepAdaptor_Curve c(TopoDS::Edge(rr.shape));
        if(c.GetType() == GeomAbs_Line) {
            const gp_Pnt p0 = c.Value(c.FirstParameter()), p1 = c.Value(c.LastParameter());
            gp_Pnt p = midpointOfEdge(TopoDS::Edge(rr.shape));
            if(r.at) p = r.at->Distance(p0) <= r.at->Distance(p1) ? p0 : p1;
            // Pointing away from the end it is anchored at, so ends meet end to end.
            gp_Dir d = c.Line().Direction();
            if(r.at && p.Distance(p1) < 1e-9) d.Reverse();
            return AlignAnchor{AlignKind::Line, p, d, true};
        }
        if(c.GetType() == GeomAbs_Circle) return AlignAnchor{AlignKind::Line, c.Circle().Location(), c.Circle().Axis().Direction(), false};
        status.merge(Status::error("that edge is curved: pick a straight edge, a circle or a vertex"));
        return std::nullopt;
    }
    case TopAbs_FACE: {
        const TopoDS_Face f = TopoDS::Face(rr.shape);
        gp_Pln pl;
        if(planeOfFace(f, pl)) return AlignAnchor{AlignKind::Plane, centroidOfFace(f), pl.Axis().Direction(), true};
        BRepAdaptor_Surface s(f);
        if(s.GetType() == GeomAbs_Cylinder || s.GetType() == GeomAbs_Cone) {
            const gp_Ax1 ax = s.GetType() == GeomAbs_Cylinder ? s.Cylinder().Axis() : s.Cone().Axis();
            // On the axis, level with the middle of the face.
            const gp_Pnt mid = centroidOfFace(f);
            const gp_Pnt on = ax.Location().Translated(gp_Vec(ax.Direction()) * gp_Vec(ax.Location(), mid).Dot(gp_Vec(ax.Direction())));
            return AlignAnchor{AlignKind::Line, on, ax.Direction(), false};
        }
        status.merge(Status::error("that face is curved: pick a flat face, a round face (its axis) or an edge"));
        return std::nullopt;
    }
    default: break;
    }
    status.merge(Status::error("pick a vertex, an edge or a face"));
    return std::nullopt;
}

std::vector<ParamDef> AlignFeature::params() const {
    std::vector<ParamDef> out;
    if(!angle.empty()) out.push_back({angle.name, angle.expr, ValueKind::Angle, id, name + " Angle"});
    return out;
}

std::vector<FeatureId> AlignFeature::dependencies() const {
    std::vector<FeatureId> d;
    for(const AlignRef *r : {&from, &to})
        if(r->plane && r->plane->kind == PlaneRef::Kind::Construction) d.push_back(r->plane->plane);
    return d;
}

json AlignFeature::dataToJson() const {
    json j{{"bodies", bodies}, {"from", from.toJson()}, {"to", to.toJson()}};
    if(flip) j["flip"] = true;
    if(!angle.empty()) j["angle"] = angle.toJson();
    if(copy) j["copy"] = true;
    return j;
}

void AlignFeature::dataFromJson(const json &j) {
    bodies = jget<std::vector<std::string>>(j, "bodies", {});
    from = AlignRef::fromJson(j.value("from", json()));
    to = AlignRef::fromJson(j.value("to", json()));
    flip = jget<bool>(j, "flip", false);
    angle = ParamSlot::fromJson(j.value("angle", json()));
    copy = jget<bool>(j, "copy", false);
}

bool AlignFeature::placement(const ModelState &state, const ComputeContext &ctx, gp_Trsf &out, AlignAnchor *fromAnchor,
                             AlignAnchor *toAnchor, Status &st) const {
    if(bodies.empty()) {
        st.merge(Status::error("pick the bodies to move"));
        return false;
    }
    for(const BodyId &b : bodies)
        if(!state.body(b)) {
            st.merge(Status::error("body " + b + " is gone"));
            return false;
        }
    auto onMoved = [&](const AlignRef &r) {
        if(r.ref.empty()) return false;
        const ResolvedRef rr = resolveRef(state, r.ref);
        return rr.ok && rr.body && std::find(bodies.begin(), bodies.end(), rr.body->id) != bodies.end();
    };
    if(from.empty()) {
        st.merge(Status::error("pick a point, edge or face on the bodies to align from"));
        return false;
    }
    if(!onMoved(from)) {
        st.merge(Status::error("align from something on the bodies being moved"));
        return false;
    }
    if(to.empty()) {
        st.merge(Status::error("pick where to align to"));
        return false;
    }
    if(onMoved(to)) {
        st.merge(Status::error("align to something that stays put, not on the bodies being moved"));
        return false;
    }
    const auto a = anchorOf(state, from, st), b = anchorOf(state, to, st);
    if(!a || !b) return false;
    double turn = 0;
    if(!angle.empty() && !ctx.value(angle, turn, st, ValueKind::Angle)) return false;
    std::string why;
    const auto t = alignTransform(*a, *b, flip, turn, why);
    if(!t) {
        st.merge(Status::error(why));
        return false;
    }
    out = *t;
    if(fromAnchor) *fromAnchor = *a;
    if(toAnchor) *toAnchor = *b;
    return true;
}

FeatureResult AlignFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        gp_Trsf t;
        if(!placement(*input, ctx, t, nullptr, nullptr, st)) return {input, st};
        auto out = std::make_shared<ModelState>(*input);
        const std::string prefix = "f" + std::to_string(id) + "/";
        std::vector<NamedShape> copies;
        for(const BodyId &b : bodies) {
            const NamedShape &src = input->body(b)->shape;
            if(copy) copies.push_back(movedNamed(src, t, prefix));
            else replaceBody(*out, b, movedNamed(src, t, ""));
        }
        for(const NamedShape &c : copies) addNewBodies(*out, c, id);
        return {out, st};
    });
}

} // namespace cad
