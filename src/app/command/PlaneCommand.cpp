#include "command/PlaneCommand.h"

#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <TopoDS.hxx>

#include <QComboBox>

namespace cadly {

namespace {

const QColor kActive(20, 100, 225);
const QColor kOther(120, 150, 200);
// The Rotation Axis choices: the plane's own axes (Tilt X and Tilt Y), or an edge.
const cad::PlaneRotationAxis kAxes[] = {cad::PlaneRotationAxis::LocalX, cad::PlaneRotationAxis::Edge};
const QColor kTiltX(229, 70, 58), kTiltY(61, 174, 79);

QVector3D toQ(const gp_XYZ &v) { return QVector3D(float(v.X()), float(v.Y()), float(v.Z())); }

} // namespace

PlaneCommand::PlaneCommand(const CommandContext &ctx, cad::FeatureId editing)
    : Command(ctx, editing), m_gizmo(ctx.viewport) {}

IconId PlaneCommand::iconId() const { return IconId::Plane; }

void PlaneCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::ConstructionPlaneFeature>(doc.feature(m_editing));
    m_baseField = panel.addSelection(tr("Plane"), tr("Select a plane or face"), "planeBase");
    m_offsetField = panel.addValue(tr("Distance"), cad::ValueKind::Length, evaluator(), "planeOffset");
    m_angleField = panel.addAngle(tr("Tilt X"), evaluator(), "planeAngle", kTiltX);
    m_angleYField = panel.addAngle(tr("Tilt Y"), evaluator(), "planeAngleY", kTiltY);
    m_axis = panel.addChoice(tr("Rotation Axis"), {tr("Plane axes"), tr("Edge")}, "planeAxis");
    m_edgeField = panel.addSelection(tr("Axis Edge"), tr("Select a straight edge"), "planeEdge");
    m_offset = m_original && !m_original->offset.empty() ? m_original->offset : doc.makeSlot("10 mm");
    if(m_original) {
        m_base = InputRef::ofPlane(m_original->base);
        if(!m_original->axisEdge.empty()) m_edge = InputRef::ofTopo(m_original->axisEdge);
        const bool edge = m_original->axis == cad::PlaneRotationAxis::Edge;
        m_axis->setCurrentIndex(edge ? 1 : 0);
        if(m_original->axis == cad::PlaneRotationAxis::LocalY) {
            // A plane from before two-axis tilts, turned about Y: its angle is Tilt Y.
            m_angleY = !m_original->angle.empty() ? m_original->angle : doc.makeSlot("0 deg");
            m_angle = doc.makeSlot("0 deg");
        } else {
            m_angle = !m_original->angle.empty() ? m_original->angle : doc.makeSlot("0 deg");
            m_angleY = !m_original->angleY.empty() ? m_original->angleY : doc.makeSlot("0 deg");
        }
    } else {
        m_angle = doc.makeSlot("0 deg");
        m_angleY = doc.makeSlot("0 deg");
        // A plane or planar face selected beforehand.
        for(const auto &it : m_ctx.view->selection().items())
            if(const auto p = m_ctx.view->planeRefOf(it)) {
                m_base = InputRef::ofPlane(*p);
                break;
            }
    }
    m_offsetField->setExpression(QString::fromStdString(m_offset.expr));
    m_angleField->setExpression(QString::fromStdString(m_angle.expr));
    m_angleYField->setExpression(QString::fromStdString(m_angleY.expr));
    for(ValueField *v : {m_offsetField, m_angleField, m_angleYField})
        connect(v, &ValueField::edited, this, [this] {
            updateArrow();
            emit inputsChanged();
        });
    connect(m_axis, &QComboBox::currentIndexChanged, this, [this] {
        updateRows();
        if(edgeMode() && !m_edge) activate(Field::Edge);
        updateArrow();
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
    m_gizmo.arrow().onDrag = [this](double d) {
        m_offsetField->setExpression(QString::fromStdString(SketchEditor::formatExpression(d, cad::ValueKind::Length)));
        emit inputsChanged();
    };
    m_gizmo.onRingDrag = [this](int ring, double a) {
        ValueField *f = ring == 0 ? m_angleField : m_angleYField;
        f->setExpression(QString::fromStdString(SketchEditor::formatExpression(a, cad::ValueKind::Angle)));
        updateArrow();
        emit inputsChanged();
    };
    m_gizmo.onFocusChanged = [this] { m_ctx.viewport->refreshOverlay(); };
    // The origin planes can be picked while the command runs.
    m_ctx.view->setOriginForced(true);
    updateRows();
    activate(Field::Base);
    if(m_base) {
        m_offsetField->setFocus(Qt::OtherFocusReason);
        m_offsetField->selectAll();
    }
}

bool PlaneCommand::edgeMode() const {
    return kAxes[std::clamp(m_axis->currentIndex(), 0, 1)] == cad::PlaneRotationAxis::Edge;
}

int PlaneCommand::ringOf(PlaneGizmo::Part p) const {
    const int i = p == PlaneGizmo::Part::Ring0 ? 0 : p == PlaneGizmo::Part::Ring1 ? 1 : -1;
    return i >= 0 && m_gizmo.ring(i).visible ? i : -1;
}

ValueField *PlaneCommand::canvasValue() const {
    const int i = ringOf(m_gizmo.focus());
    return i == 0 ? m_angleField : i == 1 ? m_angleYField : m_offsetField;
}

std::optional<QVector3D> PlaneCommand::canvasAnchor() const {
    const int i = ringOf(m_gizmo.focus());
    if(i >= 0) return m_gizmo.knobPoint(i);
    return m_gizmo.arrow().visible() ? std::optional<QVector3D>(m_gizmo.arrow().headPoint()) : std::nullopt;
}

QColor PlaneCommand::canvasAccent() const {
    const int i = ringOf(m_gizmo.focus());
    return i >= 0 ? m_gizmo.ring(i).color : QColor();
}

void PlaneCommand::updateRows() {
    const bool edge = edgeMode();
    m_ctx.panel->setRowVisible(m_edgeField, edge);
    m_ctx.panel->setRowVisible(m_angleYField, !edge);
    m_ctx.panel->setRowLabel(m_angleField, edge ? tr("Angle") : tr("Tilt X"));
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

// The arrow stands on the base (a face's middle, a plane's centre) along its
// normal, up to the new plane's centre; the rings turn about the plane's pivot
// (its centre), or about the edge.
void PlaneCommand::updateArrow() {
    const cad::StatePtr base = baseState();
    const std::optional<cad::PlaneRef> ref = m_base ? m_base->planeRef() : std::nullopt;
    cad::ConstructionPlaneFeature probe;
    gp_Ax3 frame;
    gp_Pnt centre;
    cad::Status st;
    if(ref) probe.base = *ref;
    const bool show = base && ref && probe.baseFrame(*base, frame, centre, st);
    m_gizmo.arrow().setVisible(show);
    m_gizmo.ring(0).visible = m_gizmo.ring(1).visible = false;
    if(show) {
        const double d = m_offsetField->value().value_or(0.0);
        const gp_Vec n(frame.Direction());
        m_gizmo.arrow().setAxis(toQ(centre.XYZ()), toQ(n.XYZ()));
        m_gizmo.arrow().setDistance(d);
        frame.Translate(n * d);
        centre.Translate(n * d);
        const gp_Pnt pivot = (!m_original || m_original->pivotAtCenter) ? centre : frame.Location();
        const double ax = m_angleField->value().value_or(0.0), ay = m_angleYField->value().value_or(0.0);
        PlaneGizmo::Ring &r0 = m_gizmo.ring(0), &r1 = m_gizmo.ring(1);
        if(!edgeMode()) {
            r0.visible = r1.visible = true;
            r0.name = tr("Tilt X");
            r0.center = r1.center = toQ(pivot.XYZ());
            r0.axis = toQ(frame.XDirection().XYZ());
            r0.zero = toQ(frame.YDirection().XYZ());
            r0.angle = ax;
            const gp_Ax3 tilted = cad::tiltedFrame(frame, pivot, ax, 0.0);
            r1.axis = toQ(tilted.YDirection().XYZ());
            r1.zero = toQ(tilted.XDirection().XYZ());
            r1.angle = ay;
        } else if(m_edge && !m_edge->topo.empty()) {
            const cad::ResolvedRef r = cad::resolveRef(*base, m_edge->topo);
            if(r.ok && r.shape.ShapeType() == TopAbs_EDGE) {
                BRepAdaptor_Curve c(TopoDS::Edge(r.shape));
                if(c.GetType() == GeomAbs_Line) {
                    // A ring about the edge, through the foot of the plane's centre.
                    const gp_Lin line = c.Line();
                    const gp_Dir dir = line.Direction();
                    const gp_Pnt foot = line.Location().Translated(
                        gp_Vec(dir) * gp_Vec(line.Location(), centre).Dot(gp_Vec(dir)));
                    gp_Vec zero(foot, centre);
                    if(zero.Magnitude() < 1e-6) zero = gp_Vec(frame.Direction()).Crossed(gp_Vec(dir));
                    if(zero.Magnitude() < 1e-6) zero = gp_Vec(frame.XDirection());
                    r0.visible = true;
                    r0.name = tr("Angle");
                    r0.center = toQ(foot.XYZ());
                    r0.axis = toQ(dir.XYZ());
                    r0.zero = toQ(zero.Normalized().XYZ());
                    r0.angle = ax;
                }
            }
        }
    }
    if(ringOf(m_gizmo.focus()) < 0 && m_gizmo.focus() != PlaneGizmo::Part::Arrow) m_gizmo.setFocus(PlaneGizmo::Part::Arrow);
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
        if(edgeMode() && !m_edge) activate(Field::Edge);
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
    const cad::PlaneRotationAxis axis = kAxes[std::clamp(m_axis->currentIndex(), 0, 1)];
    const bool edge = axis == cad::PlaneRotationAxis::Edge;
    if(edge && !m_edge) {
        why = tr("Select the straight edge to turn the plane about.");
        return nullptr;
    }
    const std::pair<ValueField *, QString> fields[] = {
        {m_offsetField, tr("Distance")}, {m_angleField, edge ? tr("Angle") : tr("Tilt X")}, {m_angleYField, tr("Tilt Y")}};
    for(const auto &[field, name] : fields)
        if(!field->valid() && (field != m_angleYField || !edge)) {
            why = tr("%1: enter a value").arg(name);
            return nullptr;
        }
    auto f = m_original ? std::static_pointer_cast<cad::ConstructionPlaneFeature>(m_original->clone())
                        : std::make_shared<cad::ConstructionPlaneFeature>();
    f->base = *ref;
    f->offset = {m_offset.name, m_offsetField->expression().toStdString()};
    f->angle = {m_angle.name, m_angleField->expression().toStdString()};
    f->angleY = edge ? cad::ParamSlot() : cad::ParamSlot{m_angleY.name, m_angleYField->expression().toStdString()};
    f->axis = axis;
    f->axisEdge = edge ? m_edge->topo : cad::TopoRef();
    return f;
}

} // namespace cadly
