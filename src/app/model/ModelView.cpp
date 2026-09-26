#include "model/ModelView.h"

#include "sketch/ProfileMesh.h"
#include "viewport/Camera.h"
#include "viewport/Viewport.h"

#include "geom/OcctUtil.h"
#include "measure/Measure.h"
#include "mesh/MeshData.h"
#include "topo/Resolver.h"

#include <BRep_Tool.hxx>

#include <QRectF>

#include <cmath>

namespace cadly {

namespace {

const QColor kHoverFace(90, 160, 240, 105);
const QColor kSelectFace(30, 115, 230, 140);
const QColor kHoverEdge(70, 145, 240);
const QColor kSelectEdge(20, 100, 225);
const QColor kProfileFill(255, 200, 120, 80);
const QColor kProfileHover(255, 178, 80, 130);
const QColor kProfileSelected(60, 135, 230, 130);

QVector3D toQ(const gp_Pnt &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); }
QVector3D toQ(const gp_XYZ &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); }

} // namespace

QColor ModelView::defaultBodyColor() { return QColor(176, 186, 198); }

ModelView::ModelView(cad::Document &doc, Viewport *viewport, QObject *parent)
    : QObject(parent), m_doc(doc), m_viewport(viewport) {
    connect(viewport, &Viewport::hoverChanged, this, &ModelView::onHover);
    connect(viewport, &Viewport::hoverMoved, this, &ModelView::onHoverMoved);
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

void ModelView::setEvaluation(EvaluationPtr e) {
    if(!e) return;
    m_eval = std::move(e);
    refresh();
    emit displayed();
}

void ModelView::showDocumentNow() {
    auto e = std::make_shared<Evaluation>();
    e->features = m_doc.features();
    e->marker = m_doc.marker();
    e->state = m_doc.displayedState();
    for(int i = 0; i < e->marker; ++i) e->statuses.push_back(m_doc.statusOf(e->features[size_t(i)]->id));
    m_eval = e;
    refresh();
    emit displayed();
}

std::set<cad::FeatureId> ModelView::consumedSketches() const {
    std::set<cad::FeatureId> used;
    if(!m_eval) return used;
    for(int i = 0; i < m_eval->marker && i < int(m_eval->features.size()); ++i) {
        const auto &f = m_eval->features[size_t(i)];
        if(f->suppressed) continue;
        for(cad::FeatureId dep : f->dependencies()) used.insert(dep);
    }
    return used;
}

void ModelView::setForcedSketches(std::set<cad::FeatureId> ids) {
    if(ids == m_forcedSketches) return;
    m_forcedSketches = std::move(ids);
    refresh();
}

bool ModelView::sketchShown(cad::FeatureId id) const {
    if(id == m_hiddenSketch) return false;
    if(m_forcedSketches.count(id)) return true;
    if(auto v = m_doc.sketchVisibility(id)) return *v;
    return !consumedSketches().count(id);
}

void ModelView::setHiddenSketch(cad::FeatureId id) { m_hiddenSketch = id; }

void ModelView::setOriginVisible(bool on) {
    if(on == m_originVisible) return;
    m_originVisible = on;
    refresh();
}

const std::vector<std::vector<QVector3D>> &
ModelView::profileTriangles(const std::shared_ptr<const cad::SketchResult> &sk) {
    auto it = m_profileCache.find(sk);
    if(it != m_profileCache.end()) return it->second;
    std::vector<std::vector<QVector3D>> tris;
    for(const auto &p : sk->profiles) tris.push_back(triangulateProfile(p, sk->frame));
    return m_profileCache[sk] = std::move(tris);
}

void ModelView::refresh() {
    if(!m_eval) {
        showDocumentNow();
        return;
    }
    m_state = m_eval->state;
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
    auto square = [&](const gp_Ax3 &f, double h, bool centred, const QColor &fill, const QColor &edge) {
        const QVector3D o = toQ(f.Location());
        const QVector3D x = toQ(f.XDirection().XYZ()) * float(h), y = toQ(f.YDirection().XYZ()) * float(h);
        const QVector3D base = centred ? o - x - y : o;
        const QVector3D X = centred ? x * 2 : x, Y = centred ? y * 2 : y;
        const QVector3D c[4] = {base, base + X, base + X + Y, base + Y};
        TriangleBatch tb;
        tb.color = fill;
        tb.triangles = {c[0], c[1], c[2], c[0], c[2], c[3]};
        scene.triangles.push_back(tb);
        LineBatch lb;
        lb.color = edge;
        lb.width = 1.2f;
        lb.segments = {c[0], c[1], c[1], c[2], c[2], c[3], c[3], c[0]};
        scene.lines.push_back(lb);
    };
    // Construction planes: translucent squares with an outline.
    for(const auto &[fid, plane] : m_state->planes)
        square(plane->frame, plane->halfSize, true, QColor(235, 170, 70, 55), QColor(200, 130, 40));
    // The origin (Browser > Origin): planes, axes and the origin point.
    if(m_originVisible) {
        const double h = std::max(20.0, m_state->modelSize() * 0.35);
        for(auto k : {cad::PlaneRef::Kind::XY, cad::PlaneRef::Kind::XZ, cad::PlaneRef::Kind::YZ})
            square(cad::originPlaneFrame(k), h, false, QColor(240, 170, 60, 45), QColor(205, 125, 30));
        const std::pair<QVector3D, QColor> axes[] = {{QVector3D(1, 0, 0), QColor(205, 60, 50)},
                                                     {QVector3D(0, 1, 0), QColor(70, 160, 70)},
                                                     {QVector3D(0, 0, 1), QColor(50, 100, 215)}};
        for(const auto &[dir, color] : axes) {
            LineBatch lb;
            lb.color = color;
            lb.width = 1.8f;
            lb.segments = {QVector3D(), dir * float(h * 1.4)};
            scene.lines.push_back(lb);
        }
        PointBatch pb;
        pb.color = QColor(250, 200, 60);
        pb.outline = QColor(120, 80, 10);
        pb.size = 9.0f;
        pb.points = {QVector3D()};
        scene.points.push_back(pb);
    }
    // Sketches: shown until a displayed feature uses them (unless switched on
    // or off in the browser), with their profiles shaded.
    const std::set<cad::FeatureId> used = consumedSketches();
    m_shownSketches.clear();
    std::set<const cad::SketchResult *> onScreen;
    TriangleBatch profiles;
    profiles.color = kProfileFill;
    for(const auto &[fid, sk] : m_state->sketches) {
        if(fid == m_hiddenSketch) continue;
        const auto vis = m_doc.sketchVisibility(fid);
        if(!m_forcedSketches.count(fid) && !(vis ? *vis : !used.count(fid))) continue;
        m_shownSketches.insert(fid);
        onScreen.insert(sk.get());
        // Only shown sketches are triangulated (each result once).
        for(const auto &t : profileTriangles(sk)) profiles.triangles.insert(profiles.triangles.end(), t.begin(), t.end());
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
    if(!profiles.triangles.empty()) scene.triangles.push_back(profiles);
    for(auto it = m_profileCache.begin(); it != m_profileCache.end();)
        it = onScreen.count(it->first.get()) ? std::next(it) : m_profileCache.erase(it);
    m_viewport->setContent(scene, targets);
    pruneSelection();
    updateHighlights();
    updateStats();
}

// --- profiles --------------------------------------------------------------------

const cad::Profile *ModelView::profileOf(const SelectionItem &it) const {
    if(it.kind != SelectionItem::Kind::Profile || !m_state) return nullptr;
    auto sk = m_state->sketches.find(it.feature);
    return sk == m_state->sketches.end() ? nullptr : sk->second->profileByKey(it.key);
}

std::optional<ModelView::ProfilePick> ModelView::pickProfile(QPointF px) const {
    if(!m_state || !m_selectProfiles) return std::nullopt;
    if(px.x() < 0 || px.y() < 0 || px.x() > m_viewport->width() || px.y() > m_viewport->height()) return std::nullopt;
    Camera cam = m_viewport->camera();
    cam.viewport = m_viewport->size();
    QVector3D o, d;
    cam.ray(px, o, d);
    std::optional<ProfilePick> best;
    double bestArea = 0.0;
    for(cad::FeatureId fid : m_shownSketches) {
        auto skIt = m_state->sketches.find(fid);
        if(skIt == m_state->sketches.end()) continue;
        const auto &sk = skIt->second;
        const gp_Ax3 &f = sk->frame;
        const QVector3D n = toQ(f.Direction().XYZ()), p0 = toQ(f.Location());
        const float den = QVector3D::dotProduct(d, n);
        if(std::fabs(den) < 1e-7f) continue;
        const float t = QVector3D::dotProduct(p0 - o, n) / den;
        if(t < 0 && !cam.orthographic) continue;
        const QVector3D hit = o + d * t;
        const cad::Vec2 q = sk->toSketch(gp_Pnt(hit.x(), hit.y(), hit.z()));
        for(const auto &p : sk->profiles) {
            if(!p.contains(q)) continue;
            const double area = std::fabs(p.area);
            // Nearest plane first; within one sketch the innermost region.
            const float tol = 1e-4f * std::fabs(t) + 1e-4f;
            if(!best || t < best->rayT - tol || (std::fabs(t - best->rayT) <= tol && area < bestArea)) {
                best = ProfilePick{fid, p.key, p.sample, t};
                bestArea = area;
            }
        }
    }
    return best;
}

// --- selection -------------------------------------------------------------------

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
        case SelectionItem::Kind::Profile: ok = profileOf(it) != nullptr; break;
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

void ModelView::setSelectable(bool faces, bool edges, bool vertices, bool bodies, bool profiles) {
    PickOptions &o = m_viewport->pickOptions();
    o.faces = faces;
    o.edges = edges;
    o.vertices = vertices;
    m_selectBodies = bodies;
    m_selectProfiles = profiles;
    if(!profiles && m_profileHover) {
        m_profileHover.reset();
        updateHighlights();
    }
}

void ModelView::onHover(const PickHit &hit) {
    m_hover = hit;
    updateHighlights();
}

void ModelView::onHoverMoved(const PickHit &hit) {
    std::optional<ProfilePick> p = pickProfile(hit.screen);
    // A face in front of the sketch plane hides the profile.
    if(p && hit.valid() && hit.kind == PickHit::Kind::Face && hit.rayT < p->rayT * (1.0f - 1e-4f) - 1e-3f) p.reset();
    if(p && hit.valid() && hit.kind != PickHit::Kind::Face) p.reset(); // edges and vertices win
    const bool same = (p.has_value() == m_profileHover.has_value()) &&
                      (!p || (p->sketch == m_profileHover->sketch && p->key == m_profileHover->key));
    if(same) return;
    m_profileHover = p;
    updateHighlights();
}

void ModelView::onClicked(const PickHit &hit, Qt::KeyboardModifiers mods) {
    const bool additive = mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
    SelectionItem it;
    bool any = false;
    // A profile wins over the face it is drawn on, as in Fusion 360.
    const auto p = pickProfile(hit.screen);
    const bool edgeOrVertex = hit.kind == PickHit::Kind::Edge || hit.kind == PickHit::Kind::Vertex;
    if(p && !edgeOrVertex && (!hit.valid() || p->rayT <= hit.rayT * (1.0f + 1e-4f) + 1e-3f)) {
        it.kind = SelectionItem::Kind::Profile;
        it.feature = p->sketch;
        it.key = p->key;
        any = true;
    } else if(hit.valid()) {
        it = SelectionItem::fromPick(hit);
        any = true;
    }
    if(!any) {
        if(!additive && !m_selection.empty()) {
            clearSelection();
            emit userSelectionChanged();
        }
        return;
    }
    if(additive) {
        m_selection.toggle(it);
    } else {
        m_selection.clear();
        m_selection.add(it);
    }
    updateHighlights();
    updateStats();
    emit selectionChanged();
    emit userSelectionChanged();
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
    emit userSelectionChanged();
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
    emit userSelectionChanged();
}

void ModelView::updateHighlights() {
    std::vector<FaceHighlight> faces;
    std::vector<EdgeHighlight> edges;
    std::vector<PointBatch> points;
    std::vector<TriangleBatch> tris;
    auto meshOf = [&](const cad::BodyId &id) -> std::shared_ptr<const cad::MeshData> {
        const cad::Body *b = bodyById(id);
        return b ? b->mesh() : nullptr;
    };
    auto addProfile = [&](cad::FeatureId sketch, const std::string &key, const QColor &color) {
        if(!m_state) return;
        auto sk = m_state->sketches.find(sketch);
        if(sk == m_state->sketches.end()) return;
        const auto &all = profileTriangles(sk->second);
        for(size_t i = 0; i < sk->second->profiles.size() && i < all.size(); ++i) {
            if(sk->second->profiles[i].key != key) continue;
            TriangleBatch tb;
            tb.color = color;
            tb.triangles = all[i];
            tris.push_back(std::move(tb));
        }
    };
    auto addItem = [&](const SelectionItem &it, bool hover) {
        if(it.kind == SelectionItem::Kind::Profile) {
            addProfile(it.feature, it.key, hover ? kProfileHover : kProfileSelected);
            return;
        }
        auto mesh = meshOf(it.body);
        if(!mesh) return;
        const QColor faceColor = hover ? kHoverFace : kSelectFace;
        const QColor edgeColor = hover ? kHoverEdge : kSelectEdge;
        switch(it.kind) {
        case SelectionItem::Kind::Face:
            faces.push_back({mesh, it.index, faceColor});
            break;
        case SelectionItem::Kind::Edge:
            edges.push_back({mesh, it.index, edgeColor, hover ? 3.0f : 3.5f});
            break;
        case SelectionItem::Kind::Vertex:
            if(size_t(it.index) * 3 <= mesh->vertexPoints.size()) {
                PointBatch pb;
                pb.color = edgeColor;
                pb.outline = Qt::white;
                pb.size = 10.0f;
                const float *v = &mesh->vertexPoints[size_t(it.index - 1) * 3];
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
    for(const auto &it : m_selection.items()) addItem(it, false);
    if(m_profileHover) {
        SelectionItem h;
        h.kind = SelectionItem::Kind::Profile;
        h.feature = m_profileHover->sketch;
        h.key = m_profileHover->key;
        if(!m_selection.contains(h)) addItem(h, true);
    } else if(m_hover.valid()) {
        const SelectionItem h = SelectionItem::fromPick(m_hover);
        if(!m_selection.contains(h)) addItem(h, true);
    }
    m_viewport->setHighlights(std::move(faces), std::move(edges), std::move(points), std::move(tris));
}

void ModelView::updateStats() {
    std::vector<cad::Measurement> m;
    const auto &items = m_selection.items();
    const size_t nFaces = m_selection.count(SelectionItem::Kind::Face);
    const size_t nEdges = m_selection.count(SelectionItem::Kind::Edge);
    const size_t nBodies = m_selection.count(SelectionItem::Kind::Body);
    const size_t nVerts = m_selection.count(SelectionItem::Kind::Vertex);
    const size_t nProfiles = m_selection.count(SelectionItem::Kind::Profile);
    if(nProfiles > 0 && nProfiles == items.size()) {
        double area = 0.0;
        for(const auto &it : items)
            if(const cad::Profile *p = profileOf(it)) area += std::fabs(p->area);
        m.push_back({"", 0, cad::MeasureUnit::Text, nProfiles == 1 ? "Profile" : std::to_string(nProfiles) + " profiles"});
        m.push_back({"Area", area, cad::MeasureUnit::Area, {}});
    } else if(items.size() == 1) {
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
