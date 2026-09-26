#include "command/PlaneCommand.h"

#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <QComboBox>

namespace cadly {

namespace {

const QColor kActive(20, 100, 225);
const QColor kOther(120, 150, 200);
const cad::PlaneRotationAxis kAxes[] = {cad::PlaneRotationAxis::LocalX, cad::PlaneRotationAxis::LocalY,
                                        cad::PlaneRotationAxis::Edge};

QVector3D toQ(const gp_XYZ &v) { return QVector3D(float(v.X()), float(v.Y()), float(v.Z())); }

} // namespace

PlaneCommand::PlaneCommand(const CommandContext &ctx, cad::FeatureId editing)
    : Command(ctx, editing), m_arrow(ctx.viewport) {}

IconId PlaneCommand::iconId() const { return IconId::Plane; }

void PlaneCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::ConstructionPlaneFeature>(doc.feature(m_editing));
    m_baseField = panel.addSelection(tr("Plane"), tr("Select a plane or face"), "planeBase");
    m_offsetField = panel.addValue(tr("Distance"), cad::ValueKind::Length, evaluator(), "planeOffset");
    m_angleField = panel.addValue(tr("Angle"), cad::ValueKind::Angle, evaluator(), "planeAngle");
    m_axis = panel.addChoice(tr("Rotation Axis"), {tr("Plane X Axis"), tr("Plane Y Axis"), tr("Edge")}, "planeAxis");
    m_edgeField = panel.addSelection(tr("Axis Edge"), tr("Select a straight edge"), "planeEdge");
    if(m_original) {
        m_base = InputRef::ofPlane(m_original->base);
        if(!m_original->axisEdge.empty()) m_edge = InputRef::ofTopo(m_original->axisEdge);
        for(int i = 0; i < 3; ++i)
            if(kAxes[i] == m_original->axis) m_axis->setCurrentIndex(i);
    } else {
        // A plane or planar face selected beforehand.
        for(const auto &it : m_ctx.view->selection().items())
            if(const auto p = m_ctx.view->planeRefOf(it)) {
                m_base = InputRef::ofPlane(*p);
                break;
            }
    }
    m_offset = m_original && !m_original->offset.empty() ? m_original->offset : doc.makeSlot("10 mm");
    m_angle = m_original && !m_original->angle.empty() ? m_original->angle : doc.makeSlot("0 deg");
    m_offsetField->setExpression(QString::fromStdString(m_offset.expr));
    m_angleField->setExpression(QString::fromStdString(m_angle.expr));
    for(ValueField *v : {m_offsetField, m_angleField})
        connect(v, &ValueField::edited, this, [this] {
            updateArrow();
            emit inputsChanged();
        });
    connect(m_axis, &QComboBox::currentIndexChanged, this, [this] {
        updateRows();
        if(kAxes[m_axis->currentIndex()] == cad::PlaneRotationAxis::Edge && !m_edge) activate(Field::Edge);
        emit inputsChanged();
    });
    connect(m_baseField, &SelectionField::activated, this, [this] { activate(Field::Base); });
    connect(m_edgeField, &SelectionField::activated, this, [this] { activate(Field::Edge); });
    connect(m_baseField, &SelectionField::cleared, this, [this] {
        m_base.reset();
        activate(Field::Base);
        emit inputsChanged();
    });
    connect(m_edgeField, &SelectionField::cleared, this, [this] {
        m_edge.reset();
        activate(Field::Edge);
        emit inputsChanged();
    });
    m_arrow.onDrag = [this](double d) {
        m_offsetField->setExpression(QString::fromStdString(SketchEditor::formatExpression(d, cad::ValueKind::Length)));
        emit inputsChanged();
    };
    // The origin planes can be picked while the command runs.
    m_ctx.view->setOriginForced(true);
    updateRows();
    activate(Field::Base);
    if(m_base) {
        m_offsetField->setFocus(Qt::OtherFocusReason);
        m_offsetField->selectAll();
    }
}

void PlaneCommand::updateRows() {
    const bool edge = kAxes[std::clamp(m_axis->currentIndex(), 0, 2)] == cad::PlaneRotationAxis::Edge;
    m_ctx.panel->setRowVisible(m_edgeField, edge);
    if(!edge && m_active == Field::Edge) activate(Field::Base);
}

void PlaneCommand::activate(Field f) {
    m_active = f;
    m_baseField->setActive(f == Field::Base);
    m_edgeField->setActive(f == Field::Edge);
    SelectFilter sf;
    sf.vertices = sf.bodies = sf.profiles = false;
    if(f == Field::Base) {
        sf.edges = false;
        sf.planes = true;
        sf.planarFacesOnly = true;
    } else {
        sf.faces = false;
        sf.linearEdgesOnly = true;
    }
    m_ctx.view->setFilter(sf);
    updateMarks();
    updateArrow();
}

void PlaneCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState()) {
        if(m_base) markInput(*m_ctx.view, *base, *m_base, -1, m_active == Field::Base ? kActive : kOther, marks);
        if(m_edge) markInput(*m_ctx.view, *base, *m_edge, -1, m_active == Field::Edge ? kActive : kOther, marks);
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_baseField->setCount(m_base ? 1 : 0);
    m_edgeField->setCount(m_edge ? 1 : 0);
}

// The arrow stands on the base (a face's middle, a plane's centre) along its normal.
void PlaneCommand::updateArrow() {
    const cad::StatePtr base = baseState();
    gp_Ax3 frame;
    cad::Status st;
    const std::optional<cad::PlaneRef> ref = m_base ? m_base->planeRef() : std::nullopt;
    const bool show = base && ref && cad::resolvePlane(*base, *ref, frame, st);
    m_arrow.setVisible(show);
    if(show) {
        gp_Pnt c = frame.Location();
        if(ref->kind == cad::PlaneRef::Kind::Face) {
            const cad::ResolvedRef r = cad::resolveRef(*base, ref->face);
            if(r.ok) {
                GProp_GProps g;
                BRepGProp::SurfaceProperties(r.shape, g);
                c = g.CentreOfMass();
            }
        } else if(ref->kind == cad::PlaneRef::Kind::Construction) {
            if(auto it = base->planes.find(ref->plane); it != base->planes.end()) c = it->second->center;
        } else {
            for(const auto &[item, q] : m_ctx.view->planeQuads())
                if(item.kind == SelectionItem::Kind::Plane && item.feature == cad::kNoFeature && m_base->key == item.key) {
                    const QVector3D m = (q[0] + q[2]) * 0.5f;
                    c = gp_Pnt(m.x(), m.y(), m.z());
                }
        }
        m_arrow.setAxis(toQ(c.XYZ()), toQ(frame.Direction().XYZ()));
        m_arrow.setDistance(m_offsetField->value().value_or(0.0));
        // The value shows in the on-canvas box instead of a label.
    }
    m_ctx.viewport->refreshOverlay();
}

void PlaneCommand::previewed(const cad::StatePtr &) {
    updateMarks();
    updateArrow();
}

void PlaneCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    if(m_active == Field::Base) {
        const std::optional<cad::PlaneRef> p = m_ctx.view->planeRefOf(*item);
        if(!p) return;
        const InputRef r = InputRef::ofPlane(*p);
        // Not the plane being made (or one made after it).
        if(r.kind == SelectionItem::Kind::Plane && r.feature != cad::kNoFeature &&
           (r.feature == m_editing || r.feature == m_ctx.doc->nextFeatureId()))
            return;
        if(r.createdBy(isEditing() ? m_editing : m_ctx.doc->nextFeatureId())) return;
        m_base = r;
        if(kAxes[m_axis->currentIndex()] == cad::PlaneRotationAxis::Edge && !m_edge) activate(Field::Edge);
    } else {
        if(item->kind != SelectionItem::Kind::Edge) return;
        const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
        if(!r) return;
        m_edge = r;
    }
    updateMarks();
    updateArrow();
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> PlaneCommand::build(QString &why) {
    const std::optional<cad::PlaneRef> ref = m_base ? m_base->planeRef() : std::nullopt;
    if(!ref) {
        why = tr("Select a plane or planar face.");
        return nullptr;
    }
    const cad::PlaneRotationAxis axis = kAxes[std::clamp(m_axis->currentIndex(), 0, 2)];
    if(axis == cad::PlaneRotationAxis::Edge && !m_edge) {
        why = tr("Select the straight edge to turn the plane about.");
        return nullptr;
    }
    if(!m_offsetField->valid() || !m_angleField->valid()) {
        why = tr("%1: enter a value").arg(m_offsetField->valid() ? tr("Angle") : tr("Distance"));
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::ConstructionPlaneFeature>(m_original->clone())
                        : std::make_shared<cad::ConstructionPlaneFeature>();
    f->base = *ref;
    f->offset = {m_offset.name, m_offsetField->expression().toStdString()};
    f->angle = {m_angle.name, m_angleField->expression().toStdString()};
    f->axis = axis;
    f->axisEdge = axis == cad::PlaneRotationAxis::Edge ? m_edge->topo : cad::TopoRef();
    return f;
}

} // namespace cadly
