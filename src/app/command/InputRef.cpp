#include "command/InputRef.h"

#include "geom/OcctUtil.h"
#include "mesh/MeshData.h"
#include "topo/Resolver.h"

#include <gp_Pln.hxx>

#include <algorithm>

namespace cadly {

namespace {

QVector3D toQ(const gp_Pnt &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); }

cad::TopoKind topoKind(SelectionItem::Kind k) {
    switch(k) {
    case SelectionItem::Kind::Edge: return cad::TopoKind::Edge;
    case SelectionItem::Kind::Vertex: return cad::TopoKind::Vertex;
    default: return cad::TopoKind::Face;
    }
}

SelectionItem::Kind selectionKind(cad::TopoKind k) {
    switch(k) {
    case cad::TopoKind::Edge: return SelectionItem::Kind::Edge;
    case cad::TopoKind::Vertex: return SelectionItem::Kind::Vertex;
    case cad::TopoKind::Face: break;
    }
    return SelectionItem::Kind::Face;
}

} // namespace

bool InputRef::operator==(const InputRef &o) const {
    if(kind != o.kind) return false;
    switch(kind) {
    case SelectionItem::Kind::Face:
    case SelectionItem::Kind::Edge:
    case SelectionItem::Kind::Vertex: return topo == o.topo;
    case SelectionItem::Kind::Body: return body == o.body;
    case SelectionItem::Kind::Profile: return profile.sketch == o.profile.sketch && profile.key == o.profile.key;
    case SelectionItem::Kind::Plane: return feature == o.feature && key == o.key;
    case SelectionItem::Kind::SketchEntity: return feature == o.feature && entity == o.entity;
    }
    return false;
}

InputRef InputRef::ofTopo(const cad::TopoRef &r) {
    InputRef i;
    i.kind = selectionKind(r.kind);
    i.topo = r;
    return i;
}

InputRef InputRef::ofBody(const cad::BodyId &id) {
    InputRef i;
    i.kind = SelectionItem::Kind::Body;
    i.body = id;
    return i;
}

InputRef InputRef::ofProfile(const cad::ProfileRef &p) {
    InputRef i;
    i.kind = SelectionItem::Kind::Profile;
    i.profile = p;
    return i;
}

InputRef InputRef::ofPlane(const cad::PlaneRef &p) {
    if(p.kind == cad::PlaneRef::Kind::Face) return ofTopo(p.face);
    InputRef i;
    i.kind = SelectionItem::Kind::Plane;
    switch(p.kind) {
    case cad::PlaneRef::Kind::XY: i.key = "XY"; break;
    case cad::PlaneRef::Kind::XZ: i.key = "XZ"; break;
    case cad::PlaneRef::Kind::YZ: i.key = "YZ"; break;
    case cad::PlaneRef::Kind::Construction: i.feature = p.plane; break;
    case cad::PlaneRef::Kind::Face: break;
    }
    return i;
}

InputRef InputRef::ofSketchPoint(cad::FeatureId sketch, int point) {
    InputRef i;
    i.kind = SelectionItem::Kind::SketchEntity;
    i.feature = sketch;
    i.entity = point;
    return i;
}

bool InputRef::isTopo() const {
    return kind == SelectionItem::Kind::Face || kind == SelectionItem::Kind::Edge || kind == SelectionItem::Kind::Vertex;
}

std::optional<cad::PlaneRef> InputRef::planeRef() const {
    if(kind == SelectionItem::Kind::Face) return cad::PlaneRef::onFace(topo);
    if(kind != SelectionItem::Kind::Plane) return std::nullopt;
    if(feature != cad::kNoFeature) return cad::PlaneRef::construction(feature);
    if(key == "XY") return cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
    if(key == "XZ") return cad::PlaneRef::origin(cad::PlaneRef::Kind::XZ);
    if(key == "YZ") return cad::PlaneRef::origin(cad::PlaneRef::Kind::YZ);
    return std::nullopt;
}

bool InputRef::createdBy(cad::FeatureId self) const {
    if(!isTopo() || self == cad::kNoFeature) return false;
    return topo.name.find("f" + std::to_string(self) + "/") != std::string::npos;
}

std::optional<InputRef> inputRefOf(const ModelView &view, const SelectionItem &item) {
    const cad::StatePtr st = view.state();
    if(!st) return std::nullopt;
    switch(item.kind) {
    case SelectionItem::Kind::Face:
    case SelectionItem::Kind::Edge:
    case SelectionItem::Kind::Vertex: {
        const cad::Body *b = st->body(item.body);
        if(!b) return std::nullopt;
        const cad::TopoKind k = topoKind(item.kind);
        const int n = k == cad::TopoKind::Face   ? b->shape.faceCount()
                      : k == cad::TopoKind::Edge ? b->shape.edgeCount()
                                                 : b->shape.vertexCount();
        if(item.index < 1 || item.index > n) return std::nullopt;
        return InputRef::ofTopo(cad::makeTopoRef(*b, k, item.index));
    }
    case SelectionItem::Kind::Body:
        if(!st->body(item.body)) return std::nullopt;
        return InputRef::ofBody(item.body);
    case SelectionItem::Kind::Profile:
        if(const cad::Profile *p = view.profileOf(item)) return InputRef::ofProfile({item.feature, item.key, p->sample});
        return std::nullopt;
    case SelectionItem::Kind::Plane:
        if(auto p = view.planeRefOf(item)) return InputRef::ofPlane(*p);
        return std::nullopt;
    case SelectionItem::Kind::SketchEntity:
        if(!view.sketchPointOf(item)) return std::nullopt;
        return InputRef::ofSketchPoint(item.feature, item.entity);
    }
    return std::nullopt;
}

bool toggleRef(std::vector<InputRef> &refs, const InputRef &r) {
    auto it = std::find(refs.begin(), refs.end(), r);
    if(it != refs.end()) {
        refs.erase(it);
        return false;
    }
    refs.push_back(r);
    return true;
}

bool markInput(ModelView &view, const cad::ModelState &base, const InputRef &r, int tag, const QColor &color,
               ModelView::InputMarks &marks) {
    QColor fill = color;
    fill.setAlpha(95);
    switch(r.kind) {
    case SelectionItem::Kind::Face:
    case SelectionItem::Kind::Edge:
    case SelectionItem::Kind::Vertex: {
        const cad::ResolvedRef res = cad::resolveRef(base, r.topo);
        if(!res.ok) return false;
        // Drawn on the model before the feature (a fillet may round an edge away).
        const auto mesh = res.body->meshIfReady();
        if(!mesh) return true;
        if(r.kind == SelectionItem::Kind::Face) {
            marks.faces.push_back({mesh, res.index, fill});
        } else if(r.kind == SelectionItem::Kind::Edge) {
            marks.edges.push_back({tag, {mesh, res.index, color, 3.5f}});
        } else if(size_t(res.index) * 3 <= mesh->vertexPoints.size()) {
            const float *v = &mesh->vertexPoints[size_t(res.index - 1) * 3];
            marks.points.push_back({tag, QVector3D(v[0], v[1], v[2])});
        }
        return true;
    }
    case SelectionItem::Kind::Body: {
        const cad::Body *b = base.body(r.body);
        if(!b) return false;
        if(const auto mesh = b->meshIfReady())
            for(size_t f = 1; f <= mesh->faceRanges.size(); ++f) marks.faces.push_back({mesh, int(f), fill});
        return true;
    }
    case SelectionItem::Kind::Profile: {
        TriangleBatch tb;
        tb.color = QColor(color.red(), color.green(), color.blue(), 130);
        tb.triangles = view.profileTrianglesOf(r.profile.sketch, r.profile.key);
        if(tb.triangles.empty()) return false;
        marks.triangles.push_back(std::move(tb));
        return true;
    }
    case SelectionItem::Kind::Plane: {
        SelectionItem want;
        want.kind = SelectionItem::Kind::Plane;
        want.feature = r.feature;
        want.key = r.key;
        for(const auto &[item, q] : view.planeQuads())
            if(item == want) {
                TriangleBatch tb;
                tb.color = QColor(color.red(), color.green(), color.blue(), 110);
                tb.triangles = {q[0], q[1], q[2], q[0], q[2], q[3]};
                marks.triangles.push_back(std::move(tb));
                return true;
            }
        return r.feature == cad::kNoFeature || base.planes.count(r.feature) > 0;
    }
    case SelectionItem::Kind::SketchEntity: {
        auto sk = base.sketches.find(r.feature);
        if(sk == base.sketches.end()) return false;
        const cad::SkEntity *e = sk->second->sketch.find(r.entity);
        if(!e || e->type != cad::SkType::Point) return false;
        marks.points.push_back({tag, toQ(sk->second->toWorld({e->x, e->y}))});
        return true;
    }
    }
    return false;
}

} // namespace cadly
