#include "features/PatternFeature.h"

#include "features/BodyOps.h"
#include "features/ExtrudeFeature.h"
#include "features/HoleFeature.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

constexpr double kPiP = 3.14159265358979323846;

// A copy of `src` placed by `t`, with its faces named "<prefix>/i<k>/<name>".
NamedShape placedCopy(const NamedShape &src, const gp_Trsf &t, const std::string &prefix, int k) {
    BRepBuilderAPI_Transform tr(src.shape(), t, Standard_True);
    const TopoDS_Shape out = tr.Shape();
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(out, TopAbs_FACE, faces);
    std::vector<FaceLabel> labels(size_t(faces.Extent()));
    for(int i = 1; i <= src.faceCount(); ++i) {
        const TopoDS_Shape moved = tr.ModifiedShape(src.face(i));
        const int j = faces.FindIndex(moved);
        if(j > 0) labels[size_t(j - 1)].name = prefix + "/i" + std::to_string(k) + "/" + src.faceName(i);
    }
    for(size_t j = 0; j < labels.size(); ++j)
        if(labels[j].name.empty()) labels[j].name = prefix + "/i" + std::to_string(k) + "/new";
    return NamedShape(out, std::move(labels));
}

// One named shape holding several (their faces keep their names).
NamedShape mergeNamed(const std::vector<NamedShape> &parts) {
    std::vector<TopoDS_Shape> shapes;
    std::vector<FaceLabel> labels;
    for(const NamedShape &p : parts) {
        shapes.push_back(p.shape());
        const std::vector<FaceLabel> l = p.faceLabels();
        labels.insert(labels.end(), l.begin(), l.end());
    }
    return NamedShape(makeCompound(shapes), std::move(labels));
}

int countOf(const ComputeContext &ctx, const ParamSlot &s, int fallback, Status &st, bool &ok) {
    if(s.empty()) return fallback;
    double v = 0;
    if(!ctx.value(s, v, st, ValueKind::Scalar)) {
        ok = false;
        return 0;
    }
    return int(std::lround(v));
}

} // namespace

const char *toString(PatternKind k) {
    switch(k) {
    case PatternKind::Mirror: return "mirror";
    case PatternKind::Rectangular: return "rectangular";
    case PatternKind::Circular: return "circular";
    }
    return "rectangular";
}

PatternKind patternKindFromString(const std::string &s) {
    return s == "mirror" ? PatternKind::Mirror : s == "circular" ? PatternKind::Circular : PatternKind::Rectangular;
}

json PatternAxis::toJson() const {
    if(!builtin.empty()) return json{{"axis", builtin}};
    if(!ref.empty()) return json{{"ref", ref.toJson()}};
    return json();
}

PatternAxis PatternAxis::fromJson(const json &j) {
    PatternAxis a;
    if(!j.is_object()) return a;
    a.builtin = jget<std::string>(j, "axis", "");
    if(j.contains("ref")) a.ref = TopoRef::fromJson(j["ref"]);
    return a;
}

std::optional<gp_Ax1> resolveAxis(const ModelState &state, const PatternAxis &a, Status &status) {
    if(a.builtin == "x") return gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0));
    if(a.builtin == "y") return gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0));
    if(a.builtin == "z") return gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));
    if(a.ref.empty()) {
        status.merge(Status::error("choose a direction or axis"));
        return std::nullopt;
    }
    const ResolvedRef r = resolveRef(state, a.ref);
    status.merge(r.status);
    if(!r.ok) return std::nullopt;
    if(r.shape.ShapeType() == TopAbs_EDGE) {
        BRepAdaptor_Curve c(TopoDS::Edge(r.shape));
        if(c.GetType() == GeomAbs_Line) return gp_Ax1(c.Line().Location(), c.Line().Direction());
        if(c.GetType() == GeomAbs_Circle) return c.Circle().Axis();
    } else if(r.shape.ShapeType() == TopAbs_FACE) {
        BRepAdaptor_Surface s(TopoDS::Face(r.shape));
        if(s.GetType() == GeomAbs_Cylinder) return s.Cylinder().Axis();
        if(s.GetType() == GeomAbs_Cone) return s.Cone().Axis();
        gp_Pln p;
        if(planeOfFace(TopoDS::Face(r.shape), p)) return gp_Ax1(centroidOfFace(TopoDS::Face(r.shape)), p.Axis().Direction());
    }
    status.merge(Status::error("that edge or face gives no direction (use a straight edge, a circle or a cylinder)"));
    return std::nullopt;
}

std::vector<ParamDef> PatternFeature::params() const {
    std::vector<ParamDef> out;
    auto add = [&](const ParamSlot &s, ValueKind k, const char *label) {
        if(!s.empty()) out.push_back({s.name, s.expr, k, id, name + " " + label});
    };
    if(kind == PatternKind::Rectangular) {
        add(count1, ValueKind::Scalar, "Count");
        add(spacing1, ValueKind::Length, "Spacing");
        if(!dir2.empty()) {
            add(count2, ValueKind::Scalar, "Count 2");
            add(spacing2, ValueKind::Length, "Spacing 2");
        }
    } else if(kind == PatternKind::Circular) {
        add(count, ValueKind::Scalar, "Count");
        add(angle, ValueKind::Angle, "Angle");
    }
    return out;
}

json PatternFeature::dataToJson() const {
    json j{{"kind", toString(kind)}, {"bodies", bodies}, {"features", features}, {"join", join}};
    if(!skip.empty()) j["skip"] = skip;
    switch(kind) {
    case PatternKind::Mirror: j["plane"] = plane.toJson(); break;
    case PatternKind::Rectangular:
        j["dir1"] = dir1.toJson();
        j["count1"] = count1.toJson();
        j["spacing1"] = spacing1.toJson();
        if(!dir2.empty()) {
            j["dir2"] = dir2.toJson();
            j["count2"] = count2.toJson();
            j["spacing2"] = spacing2.toJson();
        }
        break;
    case PatternKind::Circular:
        j["axis"] = axis.toJson();
        j["count"] = count.toJson();
        j["angle"] = angle.toJson();
        if(symmetric) j["symmetric"] = true;
        break;
    }
    return j;
}

void PatternFeature::dataFromJson(const json &j) {
    kind = patternKindFromString(jget<std::string>(j, "kind", "rectangular"));
    bodies = jget<std::vector<std::string>>(j, "bodies", {});
    features = jget<std::vector<int>>(j, "features", {});
    join = jget<bool>(j, "join", true);
    skip = jget<std::vector<int>>(j, "skip", {});
    plane = PlaneRef::fromJson(j.value("plane", json()));
    dir1 = PatternAxis::fromJson(j.value("dir1", json()));
    dir2 = PatternAxis::fromJson(j.value("dir2", json()));
    count1 = ParamSlot::fromJson(j.value("count1", json()));
    spacing1 = ParamSlot::fromJson(j.value("spacing1", json()));
    count2 = ParamSlot::fromJson(j.value("count2", json()));
    spacing2 = ParamSlot::fromJson(j.value("spacing2", json()));
    axis = PatternAxis::fromJson(j.value("axis", json()));
    count = ParamSlot::fromJson(j.value("count", json()));
    angle = ParamSlot::fromJson(j.value("angle", json()));
    symmetric = jget<bool>(j, "symmetric", false);
}

std::vector<FeatureId> PatternFeature::dependencies() const {
    std::vector<FeatureId> d = features;
    if(kind == PatternKind::Mirror && plane.kind == PlaneRef::Kind::Construction) d.push_back(plane.plane);
    return d;
}

bool PatternFeature::transforms(const ModelState &state, const ComputeContext &ctx, std::vector<gp_Trsf> &out,
                                Status &st) const {
    out.clear();
    switch(kind) {
    case PatternKind::Mirror: {
        gp_Ax3 frame;
        if(!resolvePlane(state, plane, frame, st)) return false;
        gp_Trsf t;
        t.SetMirror(gp_Ax2(frame.Location(), frame.Direction()));
        out.push_back(t);
        return true;
    }
    case PatternKind::Rectangular: {
        bool ok = true;
        const int n1 = countOf(ctx, count1, 2, st, ok), n2 = dir2.empty() ? 1 : countOf(ctx, count2, 2, st, ok);
        if(!ok) return false;
        if(n1 < 1 || n2 < 1 || n1 * n2 < 2) {
            st.merge(Status::error("the pattern needs at least 2 instances"));
            return false;
        }
        if(n1 * n2 > 1000) {
            st.merge(Status::error("at most 1000 instances"));
            return false;
        }
        double s1 = 0, s2 = 0;
        if(!ctx.value(spacing1, s1, st)) return false;
        if(!dir2.empty() && !ctx.value(spacing2, s2, st)) return false;
        const auto a1 = resolveAxis(state, dir1, st);
        if(!a1) return false;
        gp_Vec v2;
        if(!dir2.empty()) {
            const auto a2 = resolveAxis(state, dir2, st);
            if(!a2) return false;
            v2 = gp_Vec(a2->Direction()) * s2;
        }
        const gp_Vec v1 = gp_Vec(a1->Direction()) * s1;
        for(int j = 0; j < n2; ++j)
            for(int i = 0; i < n1; ++i) {
                if(i == 0 && j == 0) continue;
                gp_Trsf t;
                t.SetTranslation(v1 * i + v2 * j);
                out.push_back(t);
            }
        return true;
    }
    case PatternKind::Circular: {
        bool ok = true;
        const int n = countOf(ctx, count, 4, st, ok);
        if(!ok) return false;
        if(n < 2 || n > 1000) {
            st.merge(Status::error("the pattern needs 2 to 1000 instances"));
            return false;
        }
        double total = 2 * kPiP;
        if(!angle.empty() && !ctx.value(angle, total, st, ValueKind::Angle)) return false;
        const auto ax = resolveAxis(state, axis, st);
        if(!ax) return false;
        // All the way round: n even steps; otherwise the ends are on the angle.
        const bool full = std::fabs(std::fabs(total) - 2 * kPiP) < 1e-9;
        const double step = full ? total / n : total / (n - 1);
        for(int k = 1; k < n; ++k) {
            double a = step * k;
            // Symmetric: alternately either side of the original (+1, -1, +2, -2... steps).
            if(symmetric && !full) a = step * ((k + 1) / 2) * (k % 2 ? 1 : -1);
            gp_Trsf t;
            t.SetRotation(*ax, a);
            out.push_back(t);
        }
        return true;
    }
    }
    return false;
}

FeatureResult PatternFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        std::vector<gp_Trsf> ts;
        if(!transforms(*input, ctx, ts, st)) return {input, st};
        std::vector<std::pair<int, gp_Trsf>> kept; // copy number, placement
        for(size_t k = 0; k < ts.size(); ++k)
            if(std::find(skip.begin(), skip.end(), int(k + 1)) == skip.end()) kept.push_back({int(k + 1), ts[k]});
        const std::string prefix = "f" + std::to_string(id);
        auto out = std::make_shared<ModelState>(*input);
        std::vector<TopoDS_Shape> shownParts;
        std::vector<NamedShape> newBodies; // added together at the end (one id series)

        if(!features.empty()) {
            // Repeat features: copy each one's tool solid and apply it the same way.
            if(!ctx.timeline) return {input, Status::error("the features to repeat cannot be found")};
            for(FeatureId fid : features) {
                FeaturePtr src;
                for(size_t i = 0; i < ctx.index && i < ctx.timeline->size(); ++i)
                    if((*ctx.timeline)[i]->id == fid) src = (*ctx.timeline)[i];
                if(!src) return {input, Status::error("feature " + std::to_string(fid) + " is not before the pattern")};
                if(src->suppressed) continue;
                NamedShape tool;
                BodyOperation op = BodyOperation::Cut;
                if(auto hole = std::dynamic_pointer_cast<const HoleFeature>(src)) {
                    const FeatureResult r = hole->compute(input, ctx);
                    if(!r.tool) return {input, Status::error(hole->name + " could not be repeated: " + r.status.message)};
                    tool = r.tool->shape;
                } else if(auto ex = std::dynamic_pointer_cast<const ExtrudeFeature>(src)) {
                    // The extrusion on its own: the same feature, making a new body.
                    auto solo = std::static_pointer_cast<ExtrudeFeature>(ex->clone());
                    op = ex->operation;
                    solo->operation = BodyOperation::NewBody;
                    const FeatureResult r = solo->compute(input, ctx);
                    std::vector<TopoDS_Shape> made;
                    // The bodies it made: not in the input as they are (ids may repeat).
                    for(const auto &kv : r.state->bodies) {
                        auto was = input->bodies.find(kv.first);
                        if(was == input->bodies.end() || was->second != kv.second) made.push_back(kv.second->shape.shape());
                    }
                    if(made.empty()) return {input, Status::error(ex->name + " could not be repeated: " + r.status.message)};
                    tool = NamedShape(makeCompound(made), {});
                    if(op == BodyOperation::Intersect) return {input, Status::error("intersections cannot be repeated")};
                } else {
                    return {input, Status::error(src->name + " cannot be repeated (holes and extrudes can)")};
                }
                for(const auto &[k, t] : kept) {
                    NamedShape copy = placedCopy(tool, t, prefix + "/f" + std::to_string(fid), k);
                    shownParts.push_back(copy.shape());
                    if(op == BodyOperation::NewBody) {
                        newBodies.push_back(copy);
                        continue;
                    }
                    const bool cut = op == BodyOperation::Cut;
                    std::vector<BodyId> targets = bodiesInteracting(*out, copy.shape(), !cut);
                    if(targets.empty()) {
                        if(cut) st.merge(Status::warning("copy " + std::to_string(k) + " of " + src->name + " misses the model"));
                        else newBodies.push_back(copy);
                        continue;
                    }
                    if(cut) {
                        for(const BodyId &b : targets) {
                            BooleanResult br = runBoolean(BoolOp::Cut, {&out->body(b)->shape}, {&copy}, prefix);
                            if(!br.ok) return {input, Status::error(br.error)};
                            if(!validateResult(br.shape, prefix, st)) return {input, st};
                            replaceBody(*out, b, br.shape);
                        }
                    } else {
                        // Join into the first touched body (and merge the others it bridges).
                        std::vector<const NamedShape *> tools{&copy};
                        for(size_t i = 1; i < targets.size(); ++i) tools.push_back(&out->body(targets[i])->shape);
                        BooleanResult br = runBoolean(BoolOp::Fuse, {&out->body(targets[0])->shape}, tools, prefix);
                        if(!br.ok) return {input, Status::error(br.error)};
                        if(!validateResult(br.shape, prefix, st)) return {input, st};
                        for(size_t i = 1; i < targets.size(); ++i) {
                            out->bodies.erase(targets[i]);
                            out->mergedInto[targets[i]] = targets[0];
                        }
                        replaceBody(*out, targets[0], br.shape);
                    }
                }
            }
        } else {
            // Repeat bodies.
            std::vector<BodyId> targets;
            for(const BodyId &b : bodies) {
                const BodyId real = input->resolveBodyId(b);
                if(real.empty()) st.merge(Status::warning("body " + b + " no longer exists"));
                else if(std::find(targets.begin(), targets.end(), real) == targets.end()) targets.push_back(real);
            }
            if(targets.empty()) return {input, Status::error("select the bodies or features to repeat")};
            for(const BodyId &b : targets) {
                const Body *body = input->body(b);
                std::vector<NamedShape> copies;
                for(const auto &[k, t] : kept) copies.push_back(placedCopy(body->shape, t, prefix, k));
                for(const auto &c : copies) shownParts.push_back(c.shape());
                if(copies.empty()) continue;
                if(join) {
                    std::vector<const NamedShape *> tools;
                    for(const auto &c : copies) tools.push_back(&c);
                    BooleanResult br = runBoolean(BoolOp::Fuse, {&body->shape}, tools, prefix);
                    if(!br.ok) return {input, Status::error(br.error)};
                    if(!validateResult(br.shape, prefix, st)) return {input, st};
                    // Copies that do not touch the original stay separate bodies.
                    replaceBody(*out, b, br.shape);
                } else {
                    newBodies.insert(newBodies.end(), copies.begin(), copies.end());
                }
            }
        }
        if(!newBodies.empty()) addNewBodies(*out, mergeNamed(newBodies), id);
        return {out, st, shownParts.empty() ? nullptr : toolBody(NamedShape(makeCompound(shownParts), {}))};
    });
}

} // namespace cad
