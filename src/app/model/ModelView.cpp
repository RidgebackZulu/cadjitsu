#include "model/ModelView.h"

#include "viewport/Camera.h"
#include "viewport/Viewport.h"

#include "geom/OcctUtil.h"
#include "measure/Measure.h"
#include "mesh/MeshData.h"

#include <BRep_Tool.hxx>

#include <QRectF>

namespace cadly {

namespace {

const QColor kHoverFace(90, 160, 240, 105);
const QColor kSelectFace(30, 115, 230, 140);
const QColor kHoverEdge(70, 145, 240);
const QColor kSelectEdge(20, 100, 225);

QVector3D toQ(const gp_Pnt &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); }

} // namespace

QColor ModelView::defaultBodyColor() { return QColor(176, 186, 198); }

ModelView::ModelView(cad::Document &doc, Viewport *viewport, QObject *parent)
    : QObject(parent), m_doc(doc), m_viewport(viewport) {
    connect(viewport, &Viewport::hoverChanged, this, &ModelView::onHover);
    connect(viewport, &Viewport::clicked, this, &ModelView::onClicked);
    connect(viewport, &Viewport::doubleClicked, this, &ModelView::onDoubleClicked);
    connect(viewport, &Viewport::boxSelected, this, &ModelView::onBoxSelected);
    connect(viewport, &Viewport::escapePressed, this, &ModelView::clearSelection);
}

const cad::Body *ModelView::bodyById(const cad::BodyId &id) const {
    if(!m_state) return nullptr;
    auto it = m_state->bodies.find(id);
    return it == m_state->bodies.end() ? nullptr : it->second.get();
}

void ModelView::refresh() {
    m_state = m_doc.displayedState();
    RenderScene scene;
    std::vector<PickTarget> targets;
    for(const cad::Body *b : m_state->orderedBodies()) {
        if(!m_doc.bodyVisible(b->id)) continue;
        RenderBody rb;
        rb.mesh = b->mesh();
        rb.color = defaultBodyColor();
        scene.bodies.push_back(rb);
        targets.push_back({b->id, rb.mesh});
    }
    // Construction planes: translucent squares with an outline.
    for(const auto &[fid, plane] : m_state->planes) {
        const gp_Ax3 &f = plane->frame;
        const QVector3D o = toQ(f.Location());
        const QVector3D x = toQ(gp_Pnt(f.XDirection().XYZ())) * float(plane->halfSize);
        const QVector3D y = toQ(gp_Pnt(f.YDirection().XYZ())) * float(plane->halfSize);
        const QVector3D c[4] = {o - x - y, o + x - y, o + x + y, o - x + y};
        TriangleBatch tb;
        tb.color = QColor(235, 170, 70, 55);
        tb.triangles = {c[0], c[1], c[2], c[0], c[2], c[3]};
        scene.triangles.push_back(tb);
        LineBatch lb;
        lb.color = QColor(200, 130, 40);
        lb.width = 1.2f;
        lb.segments = {c[0], c[1], c[1], c[2], c[2], c[3], c[3], c[0]};
        scene.lines.push_back(lb);
    }
    // Sketches that no later feature consumes stay visible (Fusion hides used sketches).
    for(const auto &[fid, sk] : m_state->sketches) {
        if(!m_doc.dependents(fid).empty()) continue;
        LineBatch solid, construction;
        solid.color = QColor(40, 90, 200);
        solid.width = 1.6f;
        construction.color = QColor(220, 130, 40);
        construction.width = 1.2f;
        for(const auto &e : sk->sketch.entities) {
            LineBatch &lb = e.construction ? construction : solid;
            auto P = [&](cad::Vec2 v) { return toQ(sk->toWorld(v)); };
            if(e.type == cad::SkType::Line) {
                lb.segments.push_back(P(sk->sketch.pointPos(e.a)));
                lb.segments.push_back(P(sk->sketch.pointPos(e.b)));
            } else if(e.type == cad::SkType::Circle || e.type == cad::SkType::Arc) {
                const cad::Vec2 c = sk->sketch.pointPos(e.a);
                double r = e.r, a0 = 0.0, sweep = 2 * cad::kPi;
                if(e.type == cad::SkType::Arc) {
                    const cad::Vec2 s = sk->sketch.pointPos(e.b), t = sk->sketch.pointPos(e.c);
                    r = cad::distance(c, s);
                    a0 = (s - c).angle();
                    sweep = cad::normAngle((t - c).angle() - a0);
                    if(sweep < 1e-9) sweep = 2 * cad::kPi;
                }
                const int n = std::max(8, int(sweep / (2 * cad::kPi) * 96));
                for(int i = 0; i < n; ++i) {
                    const double t0 = a0 + sweep * i / n, t1 = a0 + sweep * (i + 1) / n;
                    lb.segments.push_back(P(c + cad::Vec2(std::cos(t0), std::sin(t0)) * r));
                    lb.segments.push_back(P(c + cad::Vec2(std::cos(t1), std::sin(t1)) * r));
                }
            }
        }
        if(!solid.segments.empty()) scene.lines.push_back(solid);
        if(!construction.segments.empty()) scene.lines.push_back(construction);
    }
    m_viewport->setContent(scene, targets);
    pruneSelection();
    updateHighlights();
    updateStats();
}

void ModelView::pruneSelection() {
    SelectionSet kept;
    for(const auto &it : m_selection.items()) {
        const cad::Body *b = bodyById(it.body);
        bool ok = false;
        switch(it.kind) {
        case SelectionItem::Kind::Body: ok = b != nullptr; break;
        case SelectionItem::Kind::Face: ok = b && it.index <= b->shape.faceCount(); break;
        case SelectionItem::Kind::Edge: ok = b && it.index <= b->shape.edgeCount(); break;
        case SelectionItem::Kind::Vertex: ok = b && it.index <= b->shape.vertexCount(); break;
        default: ok = true; break;
        }
        if(ok) kept.add(it);
    }
    if(!(kept == m_selection)) {
        m_selection = kept;
        emit selectionChanged();
    }
}

void ModelView::setSelection(const SelectionSet &s) {
    m_selection = s;
    updateHighlights();
    updateStats();
    emit selectionChanged();
}

void ModelView::clearSelection() {
    if(m_selection.empty()) return;
    m_selection.clear();
    updateHighlights();
    updateStats();
    emit selectionChanged();
}

void ModelView::setSelectable(bool faces, bool edges, bool vertices, bool bodies) {
    PickOptions &o = m_viewport->pickOptions();
    o.faces = faces;
    o.edges = edges;
    o.vertices = vertices;
    m_selectBodies = bodies;
}

void ModelView::onHover(const PickHit &hit) {
    m_hover = hit;
    updateHighlights();
}

void ModelView::onClicked(const PickHit &hit, Qt::KeyboardModifiers mods) {
    const bool additive = mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
    if(!hit.valid()) {
        if(!additive) clearSelection();
        return;
    }
    const SelectionItem it = SelectionItem::fromPick(hit);
    if(additive) {
        m_selection.toggle(it);
    } else {
        m_selection.clear();
        m_selection.add(it);
    }
    updateHighlights();
    updateStats();
    emit selectionChanged();
}

void ModelView::onDoubleClicked(const PickHit &hit) {
    if(!hit.valid() || !m_selectBodies) return;
    SelectionItem it;
    it.kind = SelectionItem::Kind::Body;
    it.body = hit.body;
    m_selection.clear();
    m_selection.add(it);
    updateHighlights();
    updateStats();
    emit selectionChanged();
}

void ModelView::onBoxSelected(const QRectF &rect, bool crossing, Qt::KeyboardModifiers mods) {
    if(!m_selectBodies) return;
    const bool additive = mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
    if(!additive) m_selection.clear();
    Camera cam = m_viewport->camera();
    cam.viewport = m_viewport->size();
    for(const auto &id : bodiesInRect(cam, rect, crossing, m_viewport->pickTargets())) {
        SelectionItem it;
        it.kind = SelectionItem::Kind::Body;
        it.body = id;
        m_selection.add(it);
    }
    updateHighlights();
    updateStats();
    emit selectionChanged();
}

void ModelView::updateHighlights() {
    std::vector<FaceHighlight> faces;
    std::vector<EdgeHighlight> edges;
    std::vector<PointBatch> points;
    auto meshOf = [&](const cad::BodyId &id) -> std::shared_ptr<const cad::MeshData> {
        const cad::Body *b = bodyById(id);
        return b ? b->mesh() : nullptr;
    };
    auto addItem = [&](SelectionItem::Kind kind, const cad::BodyId &body, int index, bool hover) {
        auto mesh = meshOf(body);
        if(!mesh) return;
        const QColor faceColor = hover ? kHoverFace : kSelectFace;
        const QColor edgeColor = hover ? kHoverEdge : kSelectEdge;
        switch(kind) {
        case SelectionItem::Kind::Face:
            faces.push_back({mesh, index, faceColor});
            break;
        case SelectionItem::Kind::Edge:
            edges.push_back({mesh, index, edgeColor, hover ? 3.0f : 3.5f});
            break;
        case SelectionItem::Kind::Vertex:
            if(size_t(index) * 3 <= mesh->vertexPoints.size()) {
                PointBatch pb;
                pb.color = edgeColor;
                pb.outline = Qt::white;
                pb.size = 10.0f;
                const float *v = &mesh->vertexPoints[size_t(index - 1) * 3];
                pb.points.push_back(QVector3D(v[0], v[1], v[2]));
                points.push_back(pb);
            }
            break;
        case SelectionItem::Kind::Body:
            for(size_t f = 1; f <= mesh->faceRanges.size(); ++f) faces.push_back({mesh, int(f), faceColor});
            break;
        default:
            break;
        }
    };
    for(const auto &it : m_selection.items()) addItem(it.kind, it.body, it.index, false);
    if(m_hover.valid()) {
        const SelectionItem h = SelectionItem::fromPick(m_hover);
        if(!m_selection.contains(h)) addItem(h.kind, h.body, h.index, true);
    }
    m_viewport->setHighlights(std::move(faces), std::move(edges), std::move(points));
}

void ModelView::updateStats() {
    std::vector<cad::Measurement> m;
    const auto &items = m_selection.items();
    const size_t nFaces = m_selection.count(SelectionItem::Kind::Face);
    const size_t nEdges = m_selection.count(SelectionItem::Kind::Edge);
    const size_t nBodies = m_selection.count(SelectionItem::Kind::Body);
    const size_t nVerts = m_selection.count(SelectionItem::Kind::Vertex);
    if(items.size() == 1) {
        const SelectionItem &it = items.front();
        if(const cad::Body *b = bodyById(it.body)) {
            switch(it.kind) {
            case SelectionItem::Kind::Face: m = cad::measureFace(b->shape.face(it.index)); break;
            case SelectionItem::Kind::Edge: m = cad::measureEdge(b->shape.edge(it.index)); break;
            case SelectionItem::Kind::Vertex: m = cad::measureVertex(b->shape.vertex(it.index)); break;
            case SelectionItem::Kind::Body:
                m = cad::measureBody(b->shape.shape());
                m.insert(m.begin(), {"", 0.0, cad::MeasureUnit::Text, m_doc.bodyName(*b)});
                break;
            default: break;
            }
        }
    } else if(items.size() > 1) {
        if(nEdges == items.size()) {
            double total = 0.0;
            for(const auto &it : items)
                if(const cad::Body *b = bodyById(it.body)) total += cad::lengthOf(b->shape.edge(it.index));
            m.push_back({"", 0, cad::MeasureUnit::Text, std::to_string(nEdges) + " edges"});
            m.push_back({"Total length", total, cad::MeasureUnit::Length, {}});
        } else if(nFaces == items.size()) {
            double total = 0.0;
            for(const auto &it : items)
                if(const cad::Body *b = bodyById(it.body)) total += cad::areaOf(b->shape.face(it.index));
            m.push_back({"", 0, cad::MeasureUnit::Text, std::to_string(nFaces) + " faces"});
            m.push_back({"Total area", total, cad::MeasureUnit::Area, {}});
        } else if(nBodies == items.size()) {
            double total = 0.0;
            for(const auto &it : items)
                if(const cad::Body *b = bodyById(it.body)) total += cad::volumeOf(b->shape.shape());
            m.push_back({"", 0, cad::MeasureUnit::Text, std::to_string(nBodies) + " bodies"});
            m.push_back({"Total volume", total, cad::MeasureUnit::Volume, {}});
        } else if(nVerts == 2 && items.size() == 2) {
            const cad::Body *b1 = bodyById(items[0].body), *b2 = bodyById(items[1].body);
            if(b1 && b2) {
                const gp_Pnt p1 = BRep_Tool::Pnt(b1->shape.vertex(items[0].index));
                const gp_Pnt p2 = BRep_Tool::Pnt(b2->shape.vertex(items[1].index));
                m.push_back({"Distance", p1.Distance(p2), cad::MeasureUnit::Length, {}});
            }
        } else {
            m.push_back({"", 0, cad::MeasureUnit::Text, std::to_string(items.size()) + " selected"});
        }
    }
    const QString text = QString::fromStdString(cad::formatMeasurements(m));
    if(text != m_stats) {
        m_stats = text;
        emit statsChanged(m_stats);
    }
}

} // namespace cadly
