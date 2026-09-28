#include "model/ModelView.h"

#include "sketch/SketchText.h"
#include "sketch/ProfileMesh.h"
#include "viewport/Camera.h"
#include "viewport/Viewport.h"

#include "doc/Section.h"
#include "geom/OcctUtil.h"
#include "measure/Measure.h"
#include "mesh/MeshData.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <gp_Pln.hxx>

#include <QRectF>

#include <cmath>
#include <limits>

namespace cadjitsu {

namespace {

const QColor kHoverFace(90, 160, 240, 105);
const QColor kSelectFace(30, 115, 230, 140);
const QColor kHoverEdge(70, 145, 240);
const QColor kSelectEdge(20, 100, 225);
const QColor kProfileFill(255, 200, 120, 80);
const QColor kProfileHover(255, 178, 80, 130);
const QColor kProfileSelected(60, 135, 230, 130);
const QColor kToolColor(222, 62, 48);
const QColor kPlaneFill(235, 170, 70, 55);
const QColor kPlaneEdge(200, 130, 40);
const QColor kOriginFill(240, 170, 60, 45);
const QColor kOriginEdge(205, 125, 30);
const QColor kPlaneHover(90, 160, 240, 95);
const QColor kPlaneSelected(30, 115, 230, 125);
const QColor kSketchPoint(40, 90, 200);

constexpr float kPointTolerance = 7.0f; // pixels

QVector3D toQ(const gp_Pnt &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); }
QVector3D toQ(const gp_XYZ &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); }

const std::pair<cad::PlaneRef::Kind, const char *> kOriginPlanes[] = {
    {cad::PlaneRef::Kind::XY, "XY"}, {cad::PlaneRef::Kind::XZ, "XZ"}, {cad::PlaneRef::Kind::YZ, "YZ"}};

std::optional<cad::PlaneRef::Kind> originKind(const std::string &key) {
    for(const auto &[k, name] : kOriginPlanes)
        if(key == name) return k;
    return std::nullopt;
}

SelectionItem planeItem(cad::FeatureId construction, const std::string &origin = {}) {
    SelectionItem it;
    it.kind = SelectionItem::Kind::Plane;
    it.feature = construction;
    it.key = origin;
    return it;
}

} // namespace

QColor ModelView::defaultBodyColor() { return QColor(176, 186, 198); }

ModelView::ModelView(cad::Document &doc, Viewport *viewport, QObject *parent)
    : QObject(parent), m_doc(doc), m_viewport(viewport), m_filter(SelectFilter::idle()) {
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

bool ModelView::sketchShown(cad::FeatureId id, bool ignoreFolder) const {
    if(id == m_hiddenSketch) return false;
    if(m_forcedSketches.count(id)) return true;
    if(!ignoreFolder && !m_doc.folderVisible("sketches")) return false;
    if(auto v = m_doc.sketchVisibility(id)) return *v;
    return !consumedSketches().count(id);
}

void ModelView::setHiddenSketch(cad::FeatureId id) { m_hiddenSketch = id; }

void ModelView::setOriginVisible(bool on) {
    m_doc.setFolderVisible("origin", on); // the document's change refreshes the view
}

bool ModelView::originVisible() const { return m_doc.folderVisible("origin"); }

bool ModelView::planeShown(cad::FeatureId id) const {
    return m_originForced || (m_doc.folderVisible("construction") && m_doc.planeVisible(id));
}

void ModelView::setSectionOverride(std::optional<std::optional<cad::SectionAnalysis>> s) {
    m_sectionOverride = std::move(s);
    if(m_eval) refresh();
}

std::optional<cad::SectionAnalysis> ModelView::shownSection() const {
    if(m_sectionOverride) return *m_sectionOverride;
    if(const cad::SectionAnalysis *s = m_doc.activeSection()) return *s;
    return std::nullopt;
}

void ModelView::setOverhangAnalysis(std::optional<cad::OverhangOptions> o) {
    const bool same = o && m_overhang && o->threshold == m_overhang->threshold && o->nearBand == m_overhang->nearBand &&
                      o->flatBand == m_overhang->flatBand;
    if(!same) m_overhangCache.clear();
    if(!o && !m_overhang) return;
    m_overhang = o;
    refresh();
}

void ModelView::setOriginForced(bool on) {
    if(on == m_originForced) return;
    m_originForced = on;
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
        if(!m_doc.bodyVisible(b->id) || !m_doc.folderVisible("bodies")) continue;
        RenderBody rb;
        rb.mesh = b->mesh();
        rb.color = defaultBodyColor();
        scene.bodies.push_back(rb);
        targets.push_back({b->id, rb.mesh});
    }
    // Overhang analysis: printable surfaces faintly green, those close to the
    // limit amber, overhangs red and flat bridges blue, over the shading.
    m_overhangAreas = {};
    if(m_overhang) {
        cad::OverhangOptions o = *m_overhang;
        float lowest = std::numeric_limits<float>::max();
        for(const RenderBody &rb : scene.bodies)
            if(rb.mesh) lowest = std::min(lowest, rb.mesh->bboxMin[2]);
        o.plateZ = lowest == std::numeric_limits<float>::max() ? 0.0 : lowest;
        if(o.plateZ != m_overhang->plateZ) m_overhangCache.clear();
        m_overhang->plateZ = o.plateZ;
        const QColor colors[] = {QColor(60, 180, 90, 26), QColor(245, 170, 30, 150), QColor(225, 50, 45, 170),
                                 QColor(40, 110, 230, 170), QColor(0, 0, 0, 0)};
        std::map<std::shared_ptr<const cad::MeshData>, cad::OverhangReport> kept;
        const size_t nBodies = scene.bodies.size();
        for(size_t i = 0; i < nBodies; ++i) {
            const auto mesh = scene.bodies[i].mesh;
            if(!mesh) continue;
            auto it = m_overhangCache.find(mesh);
            const cad::OverhangReport &r = it != m_overhangCache.end() ? kept[mesh] = it->second
                                                                       : kept[mesh] = cad::analyzeOverhangs(*mesh, o, true);
            for(size_t k = 0; k < r.area.size(); ++k) m_overhangAreas[k] += r.area[k];
            for(size_t k = 0; k < r.triangles.size(); ++k) {
                if(r.triangles[k].empty() || colors[k].alpha() == 0) continue;
                TriangleBatch tb;
                tb.color = colors[k];
                tb.triangles.reserve(r.triangles[k].size() / 3);
                for(size_t j = 0; j + 2 < r.triangles[k].size(); j += 3)
                    tb.triangles.emplace_back(r.triangles[k][j], r.triangles[k][j + 1], r.triangles[k][j + 2]);
                scene.triangles.push_back(std::move(tb));
            }
        }
        m_overhangCache = std::move(kept);
    }
    // A previewed cut or hole shows the material it removes, translucent red
    // (not pickable).
    if(m_eval->preview && m_eval->tool) {
        RenderBody tool;
        tool.mesh = m_eval->tool->mesh();
        tool.color = kToolColor;
        tool.opacity = 0.38f;
        scene.bodies.push_back(tool);
    }
    m_planeQuads.clear();
    auto square = [&](const Quad &c, const QColor &fill, const QColor &edge) {
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
    // Hidden ones (or all, with the Construction folder off) are neither drawn nor
    // pickable, except while a command asks for a plane.
    for(const auto &[fid, plane] : m_state->planes) {
        if(!planeShown(fid)) continue;
        const Quad q = constructionPlaneQuad(*plane);
        square(q, kPlaneFill, kPlaneEdge);
        m_planeQuads.push_back({planeItem(fid), q});
    }
    // The origin (Browser > Origin): planes, axes and the origin point.
    if(originPlanesShown()) {
        const double h = std::max(20.0, m_state->modelSize() * 0.35);
        for(const auto &[k, name] : kOriginPlanes) {
            const Quad q = originPlaneQuad(k, float(h));
            square(q, kOriginFill, kOriginEdge);
            m_planeQuads.push_back({planeItem(cad::kNoFeature, name), q});
        }
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
    PointBatch points;
    points.color = kSketchPoint;
    points.outline = Qt::white;
    points.size = 6.0f;
    for(const auto &[fid, sk] : m_state->sketches) {
        if(fid == m_hiddenSketch) continue;
        const auto vis = m_doc.sketchVisibility(fid);
        if(!m_forcedSketches.count(fid) &&
           (!m_doc.folderVisible("sketches") || !(vis ? *vis : !used.count(fid))))
            continue;
        m_shownSketches.insert(fid);
        onScreen.insert(sk.get());
        // Only shown sketches are triangulated (each result once).
        for(const auto &t : profileTriangles(sk)) profiles.triangles.insert(profiles.triangles.end(), t.begin(), t.end());
        LineBatch solid, construction;
        solid.color = QColor(40, 90, 200);
        solid.width = 1.6f;
        construction.color = QColor(220, 130, 40);
        construction.width = 1.2f;
        std::set<int> onCurves;
        for(const auto &e : sk->sketch.entities) {
            LineBatch &lb = e.construction ? construction : solid;
            auto P = [&](cad::Vec2 v) { return toQ(sk->toWorld(v)); };
            if(e.type == cad::SkType::Line) {
                lb.segments.push_back(P(sk->sketch.pointPos(e.a)));
                lb.segments.push_back(P(sk->sketch.pointPos(e.b)));
                onCurves.insert({e.a, e.b});
            } else if(e.type == cad::SkType::Circle || e.type == cad::SkType::Arc) {
                const cad::Vec2 c = sk->sketch.pointPos(e.a);
                double r = e.r, a0 = 0.0, sweep = 2 * cad::kPi;
                if(e.type == cad::SkType::Arc) {
                    const cad::Vec2 s = sk->sketch.pointPos(e.b), t = sk->sketch.pointPos(e.c);
                    r = cad::distance(c, s);
                    a0 = (s - c).angle();
                    sweep = cad::normAngle((t - c).angle() - a0);
                    if(sweep < 1e-9) sweep = 2 * cad::kPi;
                    onCurves.insert({e.b, e.c});
                }
                onCurves.insert(e.a);
                const int n = std::max(8, int(sweep / (2 * cad::kPi) * 96));
                for(int i = 0; i < n; ++i) {
                    const double t0 = a0 + sweep * i / n, t1 = a0 + sweep * (i + 1) / n;
                    lb.segments.push_back(P(c + cad::Vec2(std::cos(t0), std::sin(t0)) * r));
                    lb.segments.push_back(P(c + cad::Vec2(std::cos(t1), std::sin(t1)) * r));
                }
            } else if(e.isText()) {
                onCurves.insert(e.a);
                for(const auto &piece : cad::sketchTextLetters(sk->sketch, e).pieces)
                    for(const auto &loop : piece)
                        for(size_t i = 0; i < loop.size(); ++i) {
                            lb.segments.push_back(P(loop[i]));
                            lb.segments.push_back(P(loop[(i + 1) % loop.size()]));
                        }
            }
        }
        // Sketch points: standalone ones always; all of them while points can be picked.
        for(const auto &e : sk->sketch.entities)
            if(e.type == cad::SkType::Point && (m_filter.sketchPoints || !onCurves.count(e.id)))
                points.points.push_back(toQ(sk->toWorld({e.x, e.y})));
        if(!solid.segments.empty()) scene.lines.push_back(solid);
        if(!construction.segments.empty()) scene.lines.push_back(construction);
    }
    if(!profiles.triangles.empty()) scene.triangles.push_back(profiles);
    if(!points.points.empty()) scene.points.push_back(points);
    for(auto it = m_profileCache.begin(); it != m_profileCache.end();)
        it = onScreen.count(it->first.get()) ? std::next(it) : m_profileCache.erase(it);

    // Section analysis: cut the model by the plane (it follows the model) and
    // close the cut bodies with caps.
    m_clip.reset();
    if(const auto section = shownSection()) {
        gp_Pln pln;
        cad::Status st;
        if(cad::resolveSection(*m_state, *section, pln, st)) {
            const gp_Dir n = pln.Axis().Direction();
            const gp_Pnt p = pln.Location();
            m_clip = QVector4D(float(n.X()), float(n.Y()), float(n.Z()),
                               float(-(n.X() * p.X() + n.Y() * p.Y() + n.Z() * p.Z())));
            // A square on the plane covering the model.
            QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
            for(const auto &b : scene.bodies)
                if(b.mesh) {
                    lo = QVector3D(std::min(lo.x(), b.mesh->bboxMin[0]), std::min(lo.y(), b.mesh->bboxMin[1]),
                                   std::min(lo.z(), b.mesh->bboxMin[2]));
                    hi = QVector3D(std::max(hi.x(), b.mesh->bboxMax[0]), std::max(hi.y(), b.mesh->bboxMax[1]),
                                   std::max(hi.z(), b.mesh->bboxMax[2]));
                }
            if(lo.x() <= hi.x()) {
                const QVector3D mid = (lo + hi) * 0.5f;
                const float h = (hi - lo).length() * 0.75f + 1.0f;
                const QVector3D nn(float(n.X()), float(n.Y()), float(n.Z()));
                const QVector3D c = mid - nn * (QVector3D::dotProduct(nn, mid) + m_clip->w());
                const QVector3D u = toQ(pln.XAxis().Direction().XYZ()) * h, v = toQ(pln.YAxis().Direction().XYZ()) * h;
                scene.capQuad = {c - u - v, c + u - v, c + u + v, c - u + v};
            }
        }
    }
    m_viewport->setClipPlane(m_clip);
    m_viewport->setContent(scene, targets);
    pruneSelection();
    updateHighlights();
    updateStats();
}

std::vector<QVector3D> ModelView::profileTrianglesOf(cad::FeatureId sketch, const std::string &key) {
    std::vector<QVector3D> out;
    if(!m_state) return out;
    auto sk = m_state->sketches.find(sketch);
    if(sk == m_state->sketches.end()) return out;
    const auto &all = profileTriangles(sk->second);
    for(size_t i = 0; i < sk->second->profiles.size() && i < all.size(); ++i)
        if(sk->second->profiles[i].key == key) out.insert(out.end(), all[i].begin(), all[i].end());
    return out;
}

// --- command input ------------------------------------------------------------------

void ModelView::setCommandInput(bool on) {
    m_commandInput = on;
    m_marks = InputMarks();
    m_markHover.reset();
    m_hover.reset();
    updateHighlights();
}

void ModelView::setInputMarks(InputMarks marks) {
    m_marks = std::move(marks);
    m_markHover.reset();
    updateHighlights();
}

std::optional<int> ModelView::markAt(QPointF px) const {
    if(!m_commandInput) return std::nullopt;
    Camera cam = m_viewport->camera();
    cam.viewport = m_viewport->size();
    std::optional<int> best;
    double bestDist = kPointTolerance;
    for(const auto &[tag, p] : m_marks.points) {
        const QPointF s = cam.project(p);
        const double d = std::hypot(s.x() - px.x(), s.y() - px.y());
        if(d <= bestDist) {
            bestDist = d;
            best = tag;
        }
    }
    if(best) return best;
    bestDist = 6.0;
    for(const auto &[tag, e] : m_marks.edges) {
        if(!e.mesh || e.edge < 1 || size_t(e.edge) > e.mesh->edgeRanges.size()) continue;
        const cad::MeshData::Range r = e.mesh->edgeRanges[size_t(e.edge - 1)];
        auto at = [&](uint32_t i) {
            const float *v = &e.mesh->edgePoints[size_t(i) * 3];
            return cam.project(QVector3D(v[0], v[1], v[2]));
        };
        for(uint32_t i = r.first; i + 1 < r.first + r.count; ++i) {
            const QPointF a = at(i), b = at(i + 1);
            const QPointF ab = b - a;
            const double len2 = QPointF::dotProduct(ab, ab);
            const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(px - a, ab) / len2, 0.0, 1.0) : 0.0;
            const QPointF q = a + ab * t;
            const double d = std::hypot(q.x() - px.x(), q.y() - px.y());
            if(d <= bestDist) {
                bestDist = d;
                best = tag;
            }
        }
    }
    return best;
}

// --- picking -----------------------------------------------------------------------

const cad::Profile *ModelView::profileOf(const SelectionItem &it) const {
    if(it.kind != SelectionItem::Kind::Profile || !m_state) return nullptr;
    auto sk = m_state->sketches.find(it.feature);
    return sk == m_state->sketches.end() ? nullptr : sk->second->profileByKey(it.key);
}

std::optional<ModelView::ProfilePick> ModelView::pickProfile(QPointF px) const {
    if(!m_state || !m_filter.profiles) return std::nullopt;
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

std::optional<std::pair<SelectionItem, float>> ModelView::pickPlane(QPointF px) const {
    if(!m_filter.planes || m_planeQuads.empty()) return std::nullopt;
    Camera cam = m_viewport->camera();
    cam.viewport = m_viewport->size();
    QVector3D o, d;
    cam.ray(px, o, d);
    std::optional<std::pair<SelectionItem, float>> best;
    for(const auto &[item, quad] : m_planeQuads)
        if(auto t = rayQuad(o, d, quad, cam.orthographic); t && (!best || *t < best->second)) best = {{item, *t}};
    return best;
}

std::optional<SelectionItem> ModelView::pickSketchPoint(QPointF px) const {
    if(!m_filter.sketchPoints || !m_state) return std::nullopt;
    Camera cam = m_viewport->camera();
    cam.viewport = m_viewport->size();
    std::optional<SelectionItem> best;
    double bestDist = kPointTolerance;
    for(cad::FeatureId fid : m_shownSketches) {
        auto skIt = m_state->sketches.find(fid);
        if(skIt == m_state->sketches.end()) continue;
        for(const auto &e : skIt->second->sketch.entities) {
            if(e.type != cad::SkType::Point) continue;
            const QPointF s = cam.project(toQ(skIt->second->toWorld({e.x, e.y})));
            const double dist = std::hypot(s.x() - px.x(), s.y() - px.y());
            if(dist > bestDist) continue;
            bestDist = dist;
            SelectionItem it;
            it.kind = SelectionItem::Kind::SketchEntity;
            it.feature = fid;
            it.entity = e.id;
            best = it;
        }
    }
    return best;
}

std::vector<QVector3D> ModelView::sketchCurvePolyline(cad::FeatureId sketch, int entity) const {
    std::vector<QVector3D> out;
    if(!m_state) return out;
    auto skIt = m_state->sketches.find(sketch);
    if(skIt == m_state->sketches.end()) return out;
    const cad::SketchResult &sk = *skIt->second;
    const cad::SkEntity *e = sk.sketch.find(entity);
    if(!e || !e->isCurve()) return out;
    auto at = [&](const cad::SkEntity *p) { return p ? cad::Vec2{p->x, p->y} : cad::Vec2{0, 0}; };
    auto world = [&](cad::Vec2 v) { return toQ(sk.toWorld(v)); };
    if(e->type == cad::SkType::Line) return {world(at(sk.sketch.find(e->a))), world(at(sk.sketch.find(e->b)))};
    const cad::Vec2 c = at(sk.sketch.find(e->a));
    double a0 = 0, a1 = 2 * cad::kPi, r = e->r;
    if(e->type == cad::SkType::Arc) {
        const cad::Vec2 p0 = at(sk.sketch.find(e->b)), p1 = at(sk.sketch.find(e->c));
        r = std::hypot(p0.x - c.x, p0.y - c.y);
        a0 = std::atan2(p0.y - c.y, p0.x - c.x);
        a1 = std::atan2(p1.y - c.y, p1.x - c.x);
        while(a1 <= a0) a1 += 2 * cad::kPi;
    }
    const int n = std::max(8, int(48 * (a1 - a0) / (2 * cad::kPi)));
    for(int i = 0; i <= n; ++i) {
        const double t = a0 + (a1 - a0) * i / n;
        out.push_back(world({c.x + r * std::cos(t), c.y + r * std::sin(t)}));
    }
    return out;
}

std::optional<SelectionItem> ModelView::pickSketchCurve(QPointF px) const {
    if(!m_filter.sketchCurves || !m_state) return std::nullopt;
    Camera cam = m_viewport->camera();
    cam.viewport = m_viewport->size();
    std::optional<SelectionItem> best;
    double bestDist = kPointTolerance;
    for(cad::FeatureId fid : m_shownSketches) {
        auto skIt = m_state->sketches.find(fid);
        if(skIt == m_state->sketches.end()) continue;
        for(const auto &e : skIt->second->sketch.entities) {
            if(!e.isCurve() || e.id < 0) continue;
            const std::vector<QVector3D> poly = sketchCurvePolyline(fid, e.id);
            for(size_t i = 0; i + 1 < poly.size(); ++i) {
                const QPointF a = cam.project(poly[i]), b = cam.project(poly[i + 1]);
                const QPointF d = b - a;
                const double len2 = QPointF::dotProduct(d, d);
                const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(px - a, d) / len2, 0.0, 1.0) : 0.0;
                const QPointF f = a + d * t;
                const double dist = std::hypot(f.x() - px.x(), f.y() - px.y());
                if(dist >= bestDist) continue;
                bestDist = dist;
                SelectionItem it;
                it.kind = SelectionItem::Kind::SketchEntity;
                it.feature = fid;
                it.entity = e.id;
                best = it;
            }
        }
    }
    return best;
}

bool ModelView::accepts(const PickHit &hit) const {
    const cad::Body *b = bodyById(hit.body);
    if(!b) return false;
    switch(hit.kind) {
    case PickHit::Kind::Face: {
        if(!m_filter.faces && !m_filter.faceSelectsBody) return false;
        gp_Pln pln;
        return !m_filter.planarFacesOnly || m_filter.faceSelectsBody || cad::planeOfFace(b->shape.face(hit.index), pln);
    }
    case PickHit::Kind::Edge:
        if(m_filter.faceSelectsBody) return true;
        if(!m_filter.edges) return false;
        return !m_filter.linearEdgesOnly || BRepAdaptor_Curve(b->shape.edge(hit.index)).GetType() == GeomAbs_Line;
    case PickHit::Kind::Vertex:
        return m_filter.vertices || m_filter.faceSelectsBody;
    case PickHit::Kind::None:
        return false;
    }
    return false;
}

std::optional<SelectionItem> ModelView::itemAt(const PickHit &hit) const {
    const QPointF px = hit.screen;
    // Sketch points are small targets: when the cursor is on one, it wins.
    if(auto p = pickSketchPoint(px)) return p;
    if(auto c = pickSketchCurve(px)) return c;
    const bool onBody = hit.valid() && accepts(hit);
    const float bodyT = onBody ? hit.rayT : std::numeric_limits<float>::infinity();
    const bool edgeOrVertex = onBody && hit.kind != PickHit::Kind::Face; // these win over regions and planes
    std::optional<SelectionItem> best;
    float bestT = bodyT;
    // A profile wins over the face it is drawn on, as in Fusion 360.
    if(!edgeOrVertex)
        if(const auto p = pickProfile(px); p && p->rayT <= bodyT * (1.0f + 1e-4f) + 1e-3f) {
            SelectionItem it;
            it.kind = SelectionItem::Kind::Profile;
            it.feature = p->sketch;
            it.key = p->key;
            best = it;
            bestT = p->rayT;
        }
    if(!edgeOrVertex && !best)
        if(const auto pl = pickPlane(px); pl && pl->second < bestT) {
            best = pl->first;
            bestT = pl->second;
        }
    if(best) return best;
    if(!onBody) return std::nullopt;
    SelectionItem it = SelectionItem::fromPick(hit);
    if(m_filter.faceSelectsBody) {
        it = SelectionItem();
        it.kind = SelectionItem::Kind::Body;
        it.body = hit.body;
    }
    return it;
}

std::optional<cad::PlaneRef> ModelView::planeRefOf(const SelectionItem &it) const {
    if(it.kind == SelectionItem::Kind::Plane) {
        if(it.feature != cad::kNoFeature) return cad::PlaneRef::construction(it.feature);
        if(auto k = originKind(it.key)) return cad::PlaneRef::origin(*k);
        return std::nullopt;
    }
    if(it.kind == SelectionItem::Kind::Face) {
        const cad::Body *b = bodyById(it.body);
        gp_Pln pln;
        if(!b || it.index < 1 || it.index > b->shape.faceCount() || !cad::planeOfFace(b->shape.face(it.index), pln))
            return std::nullopt;
        return cad::PlaneRef::onFace(cad::makeTopoRef(*b, cad::TopoKind::Face, it.index));
    }
    return std::nullopt;
}

std::optional<gp_Pnt> ModelView::sketchPointOf(const SelectionItem &it) const {
    if(it.kind != SelectionItem::Kind::SketchEntity || !m_state) return std::nullopt;
    auto sk = m_state->sketches.find(it.feature);
    if(sk == m_state->sketches.end()) return std::nullopt;
    const cad::SkEntity *e = sk->second->sketch.find(it.entity);
    if(!e || e->type != cad::SkType::Point) return std::nullopt;
    return sk->second->toWorld({e->x, e->y});
}

// --- selection -------------------------------------------------------------------

void ModelView::pruneSelection() {
    auto valid = [&](const SelectionItem &it) {
        const cad::Body *b = bodyById(it.body);
        switch(it.kind) {
        case SelectionItem::Kind::Body: return b != nullptr;
        case SelectionItem::Kind::Face: return b && it.index <= b->shape.faceCount();
        case SelectionItem::Kind::Edge: return b && it.index <= b->shape.edgeCount();
        case SelectionItem::Kind::Vertex: return b && it.index <= b->shape.vertexCount();
        case SelectionItem::Kind::Profile: return profileOf(it) != nullptr;
        case SelectionItem::Kind::SketchEntity: return sketchPointOf(it).has_value();
        case SelectionItem::Kind::Plane:
            return it.feature != cad::kNoFeature ? m_state->planes.count(it.feature) > 0 : originKind(it.key).has_value();
        }
        return true;
    };
    if(m_hover && !valid(*m_hover)) m_hover.reset();
    SelectionSet kept;
    for(const auto &it : m_selection.items())
        if(valid(it)) kept.add(it);
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

void ModelView::setFilter(const SelectFilter &f) {
    const bool pointsChanged = f.sketchPoints != m_filter.sketchPoints;
    m_filter = f;
    PickOptions &o = m_viewport->pickOptions();
    o.faces = f.faces || f.faceSelectsBody;
    o.edges = f.edges || f.faceSelectsBody;
    o.vertices = f.vertices || f.faceSelectsBody;
    m_hover.reset();
    if(pointsChanged && m_eval) refresh(); // all sketch points are drawn while they can be picked
    else updateHighlights();
}

void ModelView::setSelectable(bool faces, bool edges, bool vertices, bool bodies, bool profiles) {
    SelectFilter f = SelectFilter::idle();
    f.faces = faces;
    f.edges = edges;
    f.vertices = vertices;
    f.bodies = bodies;
    f.profiles = profiles;
    setFilter(f);
}

void ModelView::onHoverMoved(const PickHit &hit) {
    const std::optional<int> mark = markAt(hit.screen);
    const std::optional<SelectionItem> it = mark ? std::nullopt : itemAt(hit);
    if(it == m_hover && mark == m_markHover) return;
    m_hover = it;
    m_markHover = mark;
    updateHighlights();
}

void ModelView::onClicked(const PickHit &hit, Qt::KeyboardModifiers mods) {
    if(m_commandInput) {
        if(const auto tag = markAt(hit.screen)) emit markClicked(*tag);
        else emit picked(itemAt(hit), hit, mods);
        return;
    }
    const bool additive = m_filter.toggle || (mods & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier));
    const std::optional<SelectionItem> it = itemAt(hit);
    if(!it) {
        // Empty space clears, except inside a command's input.
        if(!additive && !m_selection.empty()) {
            clearSelection();
            emit userSelectionChanged();
        }
        return;
    }
    if(additive) {
        m_selection.toggle(*it);
    } else {
        m_selection.clear();
        m_selection.add(*it);
    }
    updateHighlights();
    updateStats();
    emit selectionChanged();
    emit userSelectionChanged();
}

void ModelView::onDoubleClicked(const PickHit &hit) {
    if(m_commandInput) return;
    // A double-click on a sketch (a curve or a profile) opens it for editing.
    if(const auto it = itemAt(hit);
       it && (it->kind == SelectionItem::Kind::Profile || it->kind == SelectionItem::Kind::SketchEntity)) {
        emit editSketchRequested(it->feature);
        return;
    }
    if(!hit.valid() || !m_filter.bodies) return;
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
    if(!m_filter.bodies || m_commandInput) return;
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
        const QColor faceColor = hover ? kHoverFace : kSelectFace;
        const QColor edgeColor = hover ? kHoverEdge : kSelectEdge;
        switch(it.kind) {
        case SelectionItem::Kind::Profile:
            addProfile(it.feature, it.key, hover ? kProfileHover : kProfileSelected);
            return;
        case SelectionItem::Kind::Plane:
            for(const auto &[item, q] : m_planeQuads)
                if(item == it) {
                    TriangleBatch tb;
                    tb.color = hover ? kPlaneHover : kPlaneSelected;
                    tb.triangles = {q[0], q[1], q[2], q[0], q[2], q[3]};
                    tris.push_back(std::move(tb));
                }
            return;
        case SelectionItem::Kind::SketchEntity:
            if(const auto p = sketchPointOf(it)) {
                PointBatch pb;
                pb.color = edgeColor;
                pb.outline = Qt::white;
                pb.size = 10.0f;
                pb.points.push_back(toQ(*p));
                points.push_back(pb);
            }
            return;
        default:
            break;
        }
        auto mesh = meshOf(it.body);
        if(!mesh) return;
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
    if(m_commandInput) {
        // The command's inputs; the one under the cursor lights up.
        faces.insert(faces.end(), m_marks.faces.begin(), m_marks.faces.end());
        tris.insert(tris.end(), m_marks.triangles.begin(), m_marks.triangles.end());
        for(auto [tag, e] : m_marks.edges) {
            if(tag == m_markHover) {
                e.color = kHoverEdge;
                e.width += 1.5f;
            }
            edges.push_back(e);
        }
        for(const auto &[tag, p] : m_marks.points) {
            PointBatch pb;
            pb.color = tag == m_markHover ? kHoverEdge : kSelectEdge;
            pb.outline = Qt::white;
            pb.size = tag == m_markHover ? 12.0f : 10.0f;
            pb.points.push_back(p);
            points.push_back(pb);
        }
        if(m_hover) addItem(*m_hover, true);
    } else {
        for(const auto &it : m_selection.items()) addItem(it, false);
        if(m_hover && !m_selection.contains(*m_hover)) addItem(*m_hover, true);
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
    const size_t nPoints = m_selection.count(SelectionItem::Kind::SketchEntity);
    if(nProfiles > 0 && nProfiles == items.size()) {
        double area = 0.0;
        for(const auto &it : items)
            if(const cad::Profile *p = profileOf(it)) area += std::fabs(p->area);
        m.push_back({"", 0, cad::MeasureUnit::Text, nProfiles == 1 ? "Profile" : std::to_string(nProfiles) + " profiles"});
        m.push_back({"Area", area, cad::MeasureUnit::Area, {}});
    } else if(items.size() == 1 && items.front().kind == SelectionItem::Kind::Plane) {
        const SelectionItem &it = items.front();
        std::string name = it.key + " Plane";
        if(it.feature != cad::kNoFeature && m_state->planes.count(it.feature)) name = m_state->planes.at(it.feature)->name;
        m.push_back({"", 0, cad::MeasureUnit::Text, name});
    } else if(items.size() == 1 && items.front().kind == SelectionItem::Kind::SketchEntity) {
        if(const auto p = sketchPointOf(items.front())) {
            m.push_back({"", 0, cad::MeasureUnit::Text, "Sketch point"});
            m.push_back({"X", p->X(), cad::MeasureUnit::Length, {}});
            m.push_back({"Y", p->Y(), cad::MeasureUnit::Length, {}});
            m.push_back({"Z", p->Z(), cad::MeasureUnit::Length, {}});
        }
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
        } else if(nPoints == items.size()) {
            m.push_back({"", 0, cad::MeasureUnit::Text, std::to_string(nPoints) + " sketch points"});
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

} // namespace cadjitsu
