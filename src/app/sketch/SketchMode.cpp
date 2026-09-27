#include "sketch/SketchMode.h"

#include "model/ModelView.h"
#include "model/PlaneDisplay.h"
#include "sketch/HeadsUpInput.h"
#include "sketch/SketchPalette.h"
#include "ui/Icons.h"
#include "viewport/Picker.h"
#include "viewport/Viewport.h"
#include "viewport/ViewportTool.h"

#include "features/SketchFeature.h"
#include "features/SketchRefs.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

#include <cmath>

namespace cadly {

namespace {

QVector3D toQ(const gp_XYZ &v) { return QVector3D(float(v.X()), float(v.Y()), float(v.Z())); }

const QColor kPlaneFill(240, 170, 60, 60);
const QColor kPlaneHover(240, 150, 30, 120);
const QColor kPlaneEdge(205, 125, 30);

} // namespace

// ---------------------------------------------------------------------------
// Create Sketch: pick one of the origin planes (shown as squares in the
// positive quadrants, like Fusion 360), a construction plane or a planar face.

class PlanePickTool : public ViewportTool {
public:
    struct Candidate {
        enum class Kind { None, Origin, Construction, Face };
        Kind kind = Kind::None;
        cad::PlaneRef::Kind origin = cad::PlaneRef::Kind::XY;
        cad::FeatureId plane = cad::kNoFeature;
        cad::BodyId body;
        int face = 0;
        float t = 1e30f;
        bool valid() const { return kind != Kind::None; }
        bool operator==(const Candidate &o) const {
            return kind == o.kind && origin == o.origin && plane == o.plane && body == o.body && face == o.face;
        }
    };

    explicit PlanePickTool(SketchMode &mode) : m_mode(mode) {}

    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        const Candidate c = pickAt(e->position());
        if(!c.valid()) return true;
        cad::PlaneRef ref;
        switch(c.kind) {
        case Candidate::Kind::Origin: ref = cad::PlaneRef::origin(c.origin); break;
        case Candidate::Kind::Construction: ref = cad::PlaneRef::construction(c.plane); break;
        case Candidate::Kind::Face: {
            const cad::Body *b = m_mode.modelView()->state()->body(c.body);
            if(!b) return true;
            ref = cad::PlaneRef::onFace(cad::makeTopoRef(*b, cad::TopoKind::Face, c.face));
            break;
        }
        case Candidate::Kind::None: return true;
        }
        m_mode.beginNewSketch(ref);
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        m_cursor = e->position();
        const Candidate c = pickAt(m_cursor);
        if(!(c == m_hover)) m_hover = c;
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override {
        if(e->key() != Qt::Key_Escape) return false;
        m_mode.cancelCreateSketch();
        return true;
    }
    Qt::CursorShape cursor() const override { return Qt::ArrowCursor; }

    void contribute(RenderScene &scene) override {
        const float s = originPlaneSize();
        for(auto kind : {cad::PlaneRef::Kind::XY, cad::PlaneRef::Kind::XZ, cad::PlaneRef::Kind::YZ}) {
            const gp_Ax3 f = cad::originPlaneFrame(kind);
            const QVector3D o = toQ(f.Location().XYZ()), x = toQ(f.XDirection().XYZ()) * s, y = toQ(f.YDirection().XYZ()) * s;
            const QVector3D c[4] = {o, o + x, o + x + y, o + y};
            const bool hot = m_hover.kind == Candidate::Kind::Origin && m_hover.origin == kind;
            TriangleBatch tb;
            tb.color = hot ? kPlaneHover : kPlaneFill;
            tb.triangles = {c[0], c[1], c[2], c[0], c[2], c[3]};
            scene.triangles.push_back(tb);
            LineBatch lb;
            lb.color = kPlaneEdge;
            lb.width = hot ? 2.4f : 1.3f;
            lb.segments = {c[0], c[1], c[1], c[2], c[2], c[3], c[3], c[0]};
            scene.lines.push_back(lb);
        }
        const cad::StatePtr state = m_mode.modelView()->state();
        if(!state) return;
        if(m_hover.kind == Candidate::Kind::Construction) {
            auto it = state->planes.find(m_hover.plane);
            if(it != state->planes.end()) {
                const Quad q = constructionPlaneQuad(*it->second);
                LineBatch lb;
                lb.color = kPlaneEdge;
                lb.width = 2.6f;
                lb.segments = {q[0], q[1], q[1], q[2], q[2], q[3], q[3], q[0]};
                scene.lines.push_back(lb);
            }
        }
        if(m_hover.kind == Candidate::Kind::Face)
            if(const cad::Body *b = state->body(m_hover.body))
                scene.faceHighlights.push_back({b->mesh(), m_hover.face, QColor(90, 160, 240, 110)});
    }

    void paintOverlay(QPainter &p) override {
        const QString text = m_hover.valid() ? QObject::tr("Click to sketch on this plane")
                                             : QObject::tr("Select a plane or planar face");
        QFont f = p.font();
        f.setPixelSize(12);
        p.setFont(f);
        const QRectF r(m_cursor.x() + 16, m_cursor.y() + 14, p.fontMetrics().horizontalAdvance(text) + 12, 20);
        p.setPen(QPen(QColor(150, 158, 170), 1.0));
        p.setBrush(QColor(255, 255, 255, 230));
        p.drawRoundedRect(r, 3, 3);
        p.setPen(QColor(40, 46, 56));
        p.drawText(r, Qt::AlignCenter, text);
    }

    // The origin planes' size follows the view, like Fusion's.
    float originPlaneSize() const { return std::max(2.0f, m_mode.viewport()->camera().viewHeightAtTarget() * 0.2f); }

    Candidate pickAt(QPointF px) const {
        Viewport *vp = m_mode.viewport();
        Camera cam = vp->camera();
        cam.viewport = vp->size();
        QVector3D o, d;
        cam.ray(px, o, d);
        Candidate best;
        auto planeHit = [&](const gp_Ax3 &f, float &t, float &u, float &v) {
            const QVector3D n = toQ(f.Direction().XYZ()), p0 = toQ(f.Location().XYZ());
            const float den = QVector3D::dotProduct(d, n);
            if(std::fabs(den) < 1e-6f) return false;
            t = QVector3D::dotProduct(p0 - o, n) / den;
            if(t < 0 && !cam.orthographic) return false;
            const QVector3D hit = o + d * t - p0;
            u = QVector3D::dotProduct(hit, toQ(f.XDirection().XYZ()));
            v = QVector3D::dotProduct(hit, toQ(f.YDirection().XYZ()));
            return true;
        };
        const float s = originPlaneSize();
        for(auto kind : {cad::PlaneRef::Kind::XY, cad::PlaneRef::Kind::XZ, cad::PlaneRef::Kind::YZ}) {
            float t, u, v;
            if(planeHit(cad::originPlaneFrame(kind), t, u, v) && u >= 0 && v >= 0 && u <= s && v <= s && t < best.t) {
                best = Candidate();
                best.kind = Candidate::Kind::Origin;
                best.origin = kind;
                best.t = t;
            }
        }
        const cad::StatePtr state = m_mode.modelView()->state();
        if(state) {
            for(const auto &[fid, plane] : state->planes) {
                const auto t = rayQuad(o, d, constructionPlaneQuad(*plane), cam.orthographic);
                if(t && *t < best.t) {
                    best = Candidate();
                    best.kind = Candidate::Kind::Construction;
                    best.plane = fid;
                    best.t = *t;
                }
            }
            PickOptions opts;
            opts.faces = true;
            opts.edges = opts.vertices = false;
            opts.clipPlane = vp->clipPlane();
            const PickHit hit = pick(cam, px, vp->pickTargets(), opts);
            if(hit.kind == PickHit::Kind::Face && hit.rayT < best.t) {
                gp_Pln pln;
                const cad::Body *b = state->body(hit.body);
                if(b && cad::planeOfFace(b->shape.face(hit.index), pln)) {
                    best = Candidate();
                    best.kind = Candidate::Kind::Face;
                    best.body = hit.body;
                    best.face = hit.index;
                    best.t = hit.rayT;
                } else {
                    best = Candidate(); // a curved face is in front
                }
            }
        }
        return best;
    }

private:
    SketchMode &m_mode;
    Candidate m_hover;
    QPointF m_cursor;
};

// ---------------------------------------------------------------------------

SketchMode::SketchMode(cad::Document &doc, Viewport *viewport, ModelView *modelView, QObject *parent)
    : QObject(parent), m_doc(doc), m_viewport(viewport), m_modelView(modelView) {
    m_hud = std::make_unique<HeadsUpInput>(viewport, [this](const std::string &expr, cad::ValueKind kind) {
        return m_editor ? m_editor->evaluate(expr, kind) : cad::evaluate(expr, kind);
    });
    connect(m_hud.get(), &HeadsUpInput::commitRequested, this, [this] {
        if(m_tool) m_tool->hudCommit();
    });
    connect(m_hud.get(), &HeadsUpInput::valuesChanged, this, [this] {
        if(m_tool) m_tool->hudChanged();
    });
    connect(m_hud.get(), &HeadsUpInput::cancelled, this, [this] {
        m_hud->unlockAll();
        m_viewport->setFocus(Qt::OtherFocusReason);
        if(m_tool) m_tool->hudChanged();
    });

    m_palette = new SketchPalette(viewport);
    connect(m_palette, &SketchPalette::optionsChanged, this, [this](const SketchDisplayOptions &o) {
        if(m_editor) m_editor->setOptions(o);
    });
    connect(m_palette, &SketchPalette::lookAtRequested, this, [this] { lookAt(); });
    connect(m_palette, &SketchPalette::constructionRequested, this, &SketchMode::toggleConstruction);
    connect(m_palette, &SketchPalette::finishRequested, this, &SketchMode::finish);
}

SketchMode::~SketchMode() {
    if(m_viewport && m_viewport->tool() && (m_viewport->tool() == m_tool.get() || m_viewport->tool() == m_planePick.get()))
        m_viewport->setTool(nullptr);
}

void SketchMode::retire(std::unique_ptr<SketchTool> tool) {
    if(!tool) return;
    if(m_viewport->tool() == tool.get()) m_viewport->setTool(nullptr);
    m_retiredTools.push_back(std::move(tool));
    QTimer::singleShot(0, this, [this] { m_retiredTools.clear(); });
}

void SketchMode::retire(std::unique_ptr<PlanePickTool> tool) {
    if(!tool) return;
    if(m_viewport->tool() == tool.get()) m_viewport->setTool(nullptr);
    m_retiredPicks.push_back(std::move(tool));
    QTimer::singleShot(0, this, [this] { m_retiredPicks.clear(); });
}

void SketchMode::showStatus(const QString &text) {
    if(!text.isEmpty()) emit message(text);
}

// --- create / edit / finish ------------------------------------------------------

void SketchMode::startCreateSketch() {
    if(active()) finish();
    // A planar face that is already selected is used straight away.
    const SelectionSet &sel = m_modelView->selection();
    if(sel.size() == 1 && sel.items()[0].kind == SelectionItem::Kind::Face) {
        const SelectionItem &it = sel.items()[0];
        const cad::Body *b = m_modelView->state() ? m_modelView->state()->body(it.body) : nullptr;
        gp_Pln pln;
        if(b && cad::planeOfFace(b->shape.face(it.index), pln)) {
            beginNewSketch(cad::PlaneRef::onFace(cad::makeTopoRef(*b, cad::TopoKind::Face, it.index)));
            return;
        }
    }
    cancelCreateSketch();
    m_modelView->clearSelection();
    m_modelView->setSelectable(false, false, false, false);
    m_planePick = std::make_unique<PlanePickTool>(*this);
    m_viewport->setTool(m_planePick.get());
    emit pickingPlaneChanged(true);
    emit message(tr("Select a plane or planar face to sketch on."));
}

void SketchMode::cancelCreateSketch() {
    if(!m_planePick) return;
    retire(std::move(m_planePick));
    m_modelView->setSelectable(true, true, true, true);
    m_viewport->refreshOverlay();
    emit pickingPlaneChanged(false);
}

bool SketchMode::beginNewSketch(const cad::PlaneRef &plane, bool animate) {
    cancelCreateSketch();
    if(active()) finish();
    auto f = std::make_shared<cad::SketchFeature>();
    f->plane = plane;
    const cad::FeatureId id = m_doc.addFeature(f);
    if(!enter(id, true, animate)) {
        m_doc.undo();
        m_modelView->refresh();
        return false;
    }
    return true;
}

bool SketchMode::editSketch(cad::FeatureId id, bool animate) {
    if(active()) {
        if(m_editor->featureId() == id) return true;
        finish();
    }
    cancelCreateSketch();
    return enter(id, false, animate);
}

bool SketchMode::enter(cad::FeatureId id, bool isNew, bool animate) {
    auto base = std::dynamic_pointer_cast<const cad::SketchFeature>(m_doc.feature(id));
    if(!base) return false;
    cad::Status status;
    gp_Ax3 frame;
    if(!cad::resolvePlane(*m_doc.stateAt(m_doc.indexOf(id)), base->plane, frame, status)) {
        emit message(tr("The plane of %1 cannot be found: %2")
                         .arg(QString::fromStdString(base->name), QString::fromStdString(status.message)));
        return false;
    }
    cad::Document *doc = &m_doc;
    auto provider = [doc](const cad::SketchFeature &f) {
        std::vector<cad::FeaturePtr> features = doc->features();
        bool found = false;
        for(auto &p : features)
            if(p->id == f.id) {
                p = std::make_shared<cad::SketchFeature>(f);
                found = true;
            }
        if(!found) features.push_back(std::make_shared<cad::SketchFeature>(f));
        return cad::buildParamTable(features);
    };
    m_editor = std::make_unique<SketchEditor>(m_viewport, *base, frame, provider, [doc] { return doc->allocateParamName(); });
    m_editor->setOptions(m_palette->options());
    m_isNew = isNew;
    connect(m_editor.get(), &SketchEditor::changed, this, [this] {
        emit statsChanged(m_editor->selectionStats());
        emit geometryChanged();
    });
    connect(m_editor.get(), &SketchEditor::message, this, &SketchMode::message);

    m_modelView->clearSelection();
    m_modelView->setSelectable(false, false, false, false);
    m_modelView->setHiddenSketch(id);
    m_modelView->refresh();
    m_palette->reposition();
    m_palette->show();
    emit activeChanged(true);
    setTool(SketchToolKind::Select);
    lookAt(animate);
    emit statsChanged(QString());
    return true;
}

void SketchMode::finish() {
    if(!m_editor) return;
    closeDimensionEditor();
    if(m_tool) m_tool->cancel();
    auto f = std::make_shared<cad::SketchFeature>(*m_editor->feature());
    const cad::FeatureId id = f->id;
    const auto old = std::dynamic_pointer_cast<const cad::SketchFeature>(m_doc.feature(id));
    const bool changed = !old || old->sketch.toJson() != f->sketch.toJson();
    const bool isNew = m_isNew;
    leave();
    if(changed) {
        m_doc.replaceFeature(f, "Edit " + f->name, !isNew);
        // Regions used by extrudes are looked for where they are now.
        cad::refreshProfileRefs(m_doc, id);
    }
    m_modelView->refresh();
    emit finished(id);
}

void SketchMode::leave() {
    retire(std::move(m_tool));
    m_viewport->setTool(nullptr);
    m_hud->hide();
    m_hud->unlockAll();
    m_palette->hide();
    m_modelView->setHiddenSketch(cad::kNoFeature);
    m_modelView->setSelectable(true, true, true, true);
    m_editor.reset();
    m_isNew = false;
    m_viewport->refreshOverlay();
    emit activeChanged(false);
    emit statsChanged(QString());
}

// --- tools --------------------------------------------------------------------------

void SketchMode::setTool(SketchToolKind kind) {
    if(!m_editor) return;
    closeDimensionEditor();
    if(m_tool) m_tool->cancel();
    retire(std::move(m_tool));
    m_hud->hide();
    m_hud->unlockAll();
    m_editor->clearPreview();
    m_editor->hover = {};
    m_toolKind = kind;
    m_tool = createSketchTool(*this, kind);
    m_viewport->setTool(m_tool.get());
    m_tool->activate();
    showStatus(m_tool->prompt());
    emit toolChanged(kind);
    m_viewport->refreshOverlay();
}

void SketchMode::closeDimensionEditor() {
    if(m_dimensionEdit) m_dimensionEdit->dismiss();
    m_dimensionEdit = nullptr;
}

void SketchMode::editDimension(int constraintId) {
    if(!m_editor) return;
    const cad::SkConstraint *c = m_editor->sketch().findConstraint(constraintId);
    if(!c || !cad::isDimension(c->type)) return;
    closeDimensionEditor();
    const auto rect = m_editor->dimensionRect(constraintId);
    const QPointF center = rect ? rect->center() : m_editor->toScreen(m_editor->dimensionAnchor(*c) + c->label);
    const QString text = c->driven || c->expr.empty()
                             ? QString::fromStdString(SketchEditor::formatExpression(m_editor->measuredValue(*c),
                                                                                     SketchEditor::kindOf(c->type)))
                             : QString::fromStdString(c->expr);
    SketchEditor *ed = m_editor.get();
    m_dimensionEdit = new InlineValueEditor(m_viewport, text, SketchEditor::kindOf(c->type), center,
                                            [ed, constraintId](const QString &t, QString *err) {
                                                return ed->setDimensionExpression(constraintId, t, err);
                                            });
}

bool SketchMode::editSize(int entityId) {
    if(!m_editor) return false;
    const cad::Sketch &sk = m_editor->sketch();
    const cad::SkEntity *e = sk.find(entityId);
    if(!e) return false;
    cad::SkCon type;
    switch(e->type) {
    case cad::SkType::Line: type = cad::SkCon::Distance; break;
    case cad::SkType::Circle: type = cad::SkCon::Diameter; break;
    case cad::SkType::Arc: type = cad::SkCon::Radius; break;
    default: return false;
    }
    for(const cad::SkConstraint &c : sk.constraints)
        if(c.e1 == entityId && (c.type == type || (e->type != cad::SkType::Line &&
                                                    (c.type == cad::SkCon::Diameter || c.type == cad::SkCon::Radius))) &&
           (type != cad::SkCon::Distance || c.e2 == 0)) {
            editDimension(c.id);
            return true;
        }
    // About 28 pixels beside the geometry.
    double unit = 5.0;
    const QPointF c0 = m_editor->toScreen(sk.pointPos(e->a));
    if(const auto p = m_editor->toSketch(c0), q = m_editor->toSketch(c0 + QPointF(28, 0)); p && q) unit = (*q - *p).length();
    cad::Vec2 label;
    if(e->type == cad::SkType::Line) {
        const cad::Vec2 a = sk.pointPos(e->a), b = sk.pointPos(e->b);
        if((b - a).length() < 1e-9) return false;
        label = (b - a).normalized().perp() * unit;
    } else {
        const double r = e->type == cad::SkType::Circle ? e->r : (sk.pointPos(e->b) - sk.pointPos(e->a)).length();
        label = cad::Vec2(0.7071, 0.7071) * (r + unit);
    }
    bool driven = false;
    const int id = m_editor->addDimension(type, entityId, 0, label, false, &driven);
    if(id && !driven) editDimension(id);
    return id != 0;
}

bool SketchMode::undo() {
    if(!m_editor) return false;
    closeDimensionEditor();
    if(m_tool) m_tool->cancel();
    return m_editor->undo();
}

bool SketchMode::redo() {
    if(!m_editor) return false;
    closeDimensionEditor();
    if(m_tool) m_tool->cancel();
    return m_editor->redo();
}

void SketchMode::deleteSelection() {
    if(m_editor) m_editor->deleteSelection();
}

void SketchMode::toggleConstruction() {
    if(m_editor) m_editor->toggleConstruction();
}

void SketchMode::lookAt(bool animate) {
    if(!m_editor) return;
    const gp_Ax3 &f = m_editor->frame();
    const QVector3D n = toQ(f.Direction().XYZ()), up = toQ(f.YDirection().XYZ()), o = toQ(f.Location().XYZ());
    Camera c = m_viewport->camera();
    c.viewport = m_viewport->size();
    c.setOrientation(-n, up);
    Box3 box;
    for(const auto &e : m_editor->sketch().entities)
        if(e.type == cad::SkType::Point) box.add(m_editor->toWorld({e.x, e.y}));
    if(!box.isEmpty() && box.radius() > 1e-3) {
        const float distance = c.distance;
        c.fit(box, 1.8f);
        c.distance = std::max(c.distance, std::min(distance, c.distance * 4.0f));
    } else {
        c.target -= n * QVector3D::dotProduct(c.target - o, n);
    }
    if(animate) {
        m_viewport->animateTo(c.rotation, c.target, c.distance);
    } else {
        m_viewport->camera() = c;
        m_viewport->refreshOverlay();
    }
}

bool SketchMode::cancelOperation() {
    closeDimensionEditor();
    return m_tool && m_tool->cancel();
}

} // namespace cadly
