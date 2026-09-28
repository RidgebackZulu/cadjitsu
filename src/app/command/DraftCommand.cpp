#include "command/DraftCommand.h"

#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <QCheckBox>

namespace cadjitsu {

namespace {
const QColor kFace(230, 140, 40);
const QColor kHinge(20, 100, 225);
const QColor kCandidate(120, 160, 225);
constexpr int kCandidateTag = 1000;

QVector3D toQ(const gp_XYZ &v) { return QVector3D(float(v.X()), float(v.Y()), float(v.Z())); }
} // namespace

DraftCommand::DraftCommand(const CommandContext &ctx, cad::FeatureId editing)
    : Command(ctx, editing), m_gizmo(ctx.viewport) {}

IconId DraftCommand::iconId() const { return IconId::Draft; }

void DraftCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::DraftFeature>(doc.feature(m_editing));
    m_facesField = panel.addSelection(tr("Faces"), tr("Select flat faces"), "draftFaces");
    m_hingeField = panel.addSelection(tr("Hinge Edge"), tr("Select a straight edge"), "draftHinge");
    m_angleField = panel.addAngle(tr("Angle"), evaluator(), "draftAngle", QColor(229, 70, 58));
    m_flip = panel.addCheck(tr("Lean Out"), "draftFlip");
    m_flip->setToolTip(tr("Tilt the faces out over the hinge instead of in (that makes an overhang)."));
    m_angle = m_original && !m_original->angle.empty() ? m_original->angle : doc.makeSlot("5 deg");
    m_angleField->setExpression(QString::fromStdString(m_angle.expr));
    if(m_original) {
        for(const auto &f : m_original->faces) m_faces.push_back(InputRef::ofTopo(f));
        if(!m_original->hinge.empty()) m_hinge = InputRef::ofTopo(m_original->hinge);
        m_flip->setChecked(m_original->flip);
    } else {
        for(const auto &it : m_ctx.view->selection().items())
            if(it.kind == SelectionItem::Kind::Face)
                if(auto r = inputRefOf(*m_ctx.view, it)) m_faces.push_back(*r);
    }
    connect(m_angleField, &ValueField::edited, this, [this] {
        updateRing();
        emit inputsChanged();
    });
    connect(m_flip, &QCheckBox::toggled, this, [this] {
        updateRing();
        emit inputsChanged();
    });
    connect(m_facesField, &SelectionField::activated, this, [this] { activate(Field::Faces); });
    connect(m_hingeField, &SelectionField::activated, this, [this] { activate(Field::Hinge); });
    connect(m_facesField, &SelectionField::cleared, this, [this] {
        m_faces.clear();
        activate(Field::Faces);
        emit inputsChanged();
    });
    connect(m_hingeField, &SelectionField::cleared, this, [this] {
        m_hinge.reset();
        activate(Field::Hinge);
        emit inputsChanged();
    });
    m_gizmo.arrow().setVisible(false);
    m_gizmo.ring(0).name = tr("Draft");
    m_gizmo.setFocus(PlaneGizmo::Part::Ring0);
    m_gizmo.onRingDrag = [this](int, double a) {
        m_angleField->setExpression(
            QString::fromStdString(SketchEditor::formatExpression(m_flip->isChecked() ? -a : a, cad::ValueKind::Angle)));
        emit inputsChanged();
    };
    activate(m_faces.empty() ? Field::Faces : m_hinge ? Field::Faces : Field::Hinge);
    m_angleField->setFocus(Qt::OtherFocusReason);
    m_angleField->selectAll();
}

void DraftCommand::activate(Field f) {
    m_active = f;
    m_facesField->setActive(f == Field::Faces);
    m_hingeField->setActive(f == Field::Hinge);
    SelectFilter sf;
    sf.vertices = sf.bodies = sf.profiles = false;
    if(f == Field::Faces) {
        sf.edges = false;
        sf.planarFacesOnly = true;
    } else {
        sf.faces = false;
        sf.linearEdgesOnly = true;
    }
    m_ctx.view->setFilter(sf);
    updateMarks();
    updateRing();
}

void DraftCommand::updateMarks() {
    ModelView::InputMarks marks;
    m_candidates.clear();
    if(const cad::StatePtr base = baseState()) {
        for(const auto &f : m_faces) markInput(*m_ctx.view, *base, f, -1, kFace, marks);
        if(m_hinge) markInput(*m_ctx.view, *base, *m_hinge, -1, kHinge, marks);
        // The first face's straight edges, to click as the hinge.
        if(!m_faces.empty() && !m_hinge) {
            const cad::ResolvedRef r = cad::resolveRef(*base, m_faces.front().topo);
            if(r.ok) {
                const auto mesh = r.body->meshIfReady();
                for(TopExp_Explorer ex(r.shape, TopAbs_EDGE); ex.More(); ex.Next()) {
                    if(BRepAdaptor_Curve(TopoDS::Edge(ex.Current())).GetType() != GeomAbs_Line) continue;
                    const int i = r.body->shape.indexOf(cad::TopoKind::Edge, ex.Current());
                    if(i < 1) continue;
                    const int tag = kCandidateTag + int(m_candidates.size());
                    m_candidates.push_back(cad::makeTopoRef(*r.body, cad::TopoKind::Edge, i));
                    if(mesh) marks.edges.push_back({tag, {mesh, i, kCandidate, 3.0f}});
                }
            }
        }
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_facesField->setCount(int(m_faces.size()));
    m_hingeField->setCount(m_hinge ? 1 : 0);
}

void DraftCommand::updateRing() {
    PlaneGizmo::Ring &ring = m_gizmo.ring(0);
    ring.visible = false;
    const cad::StatePtr base = baseState();
    if(base && m_hinge && !m_faces.empty()) {
        const cad::ResolvedRef h = cad::resolveRef(*base, m_hinge->topo);
        for(const auto &f : m_faces) {
            const cad::ResolvedRef rf = cad::resolveRef(*base, f.topo);
            if(!h.ok || !rf.ok || rf.body != h.body) continue;
            const auto frame = cad::draftFrame(h.body->shape.shape(), TopoDS::Face(rf.shape), TopoDS::Edge(h.shape));
            if(!frame) continue;
            const double a = m_angleField->value().value_or(0.0);
            ring.visible = true;
            ring.center = toQ(frame->middle.XYZ());
            ring.axis = toQ(frame->hingeAxis.Direction().XYZ());
            ring.zero = toQ(frame->up.XYZ());
            ring.angle = m_flip->isChecked() ? -a : a;
            break;
        }
    }
    m_ctx.viewport->refreshOverlay();
}

std::optional<QVector3D> DraftCommand::canvasAnchor() const {
    if(m_gizmo.ring(0).visible) return m_gizmo.knobPoint(0);
    return m_lastPick;
}

void DraftCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
    if(!r) return;
    if(m_active == Field::Faces) {
        if(item->kind != SelectionItem::Kind::Face) return;
        toggleRef(m_faces, *r);
        // The hinge comes next.
        if(m_faces.size() == 1 && !m_hinge) {
            activate(Field::Hinge);
            emit inputsChanged();
            return;
        }
    } else {
        if(item->kind != SelectionItem::Kind::Edge) return;
        m_hinge = *r;
        activate(Field::Faces);
    }
    updateMarks();
    updateRing();
    emit inputsChanged();
}

void DraftCommand::markClicked(int tag) {
    const int i = tag - kCandidateTag;
    if(i < 0 || i >= int(m_candidates.size())) return;
    m_hinge = InputRef::ofTopo(m_candidates[size_t(i)]);
    activate(Field::Faces);
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> DraftCommand::build(QString &why) {
    if(m_faces.empty()) {
        why = tr("Select the faces to tilt.");
        return nullptr;
    }
    if(!m_hinge) {
        why = tr("Select the hinge edge the faces turn about.");
        return nullptr;
    }
    if(!m_angleField->valid()) {
        why = tr("Angle: enter a value");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::DraftFeature>(m_original->clone())
                        : std::make_shared<cad::DraftFeature>();
    f->faces.clear();
    for(const auto &r : m_faces) f->faces.push_back(r.topo);
    f->hinge = m_hinge->topo;
    f->angle = {m_angle.name, m_angleField->expression().toStdString()};
    f->flip = m_flip->isChecked();
    return f;
}

} // namespace cadjitsu
