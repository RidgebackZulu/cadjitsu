#include "command/SectionCommand.h"

#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include "topo/Resolver.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <QCheckBox>

namespace cadly {

namespace {

const QColor kInput(20, 100, 225);

QVector3D toQ(const gp_XYZ &v) { return QVector3D(float(v.X()), float(v.Y()), float(v.Z())); }

} // namespace

SectionCommand::SectionCommand(const CommandContext &ctx, int section)
    : Command(ctx, cad::kNoFeature), m_section(section), m_arrow(ctx.viewport) {}

IconId SectionCommand::iconId() const { return IconId::Section; }

void SectionCommand::setup() {
    CommandPanel &panel = *m_ctx.panel;
    m_planeField = panel.addSelection(tr("Plane"), tr("Select a plane or face"), "sectionPlane");
    m_distanceField = panel.addValue(tr("Distance"), cad::ValueKind::Length, evaluator(), "sectionDistance");
    m_flip = panel.addCheck(tr("Flip"), "sectionFlip");
    double offset = 0.0;
    if(const cad::SectionAnalysis *s = m_ctx.doc->section(m_section)) {
        m_plane = InputRef::ofPlane(s->plane);
        offset = s->offset;
        m_flip->setChecked(s->flip);
    } else {
        m_section = 0;
        for(const auto &it : m_ctx.view->selection().items())
            if(const auto p = m_ctx.view->planeRefOf(it)) {
                m_plane = InputRef::ofPlane(*p);
                break;
            }
    }
    m_distanceField->setExpression(QString::fromStdString(SketchEditor::formatExpression(offset, cad::ValueKind::Length)));
    connect(m_distanceField, &ValueField::edited, this, [this] {
        updateArrow();
        emit inputsChanged();
    });
    connect(m_flip, &QCheckBox::toggled, this, &Command::inputsChanged);
    connect(m_planeField, &SelectionField::cleared, this, [this] {
        m_plane.reset();
        updateArrow();
        emit inputsChanged();
    });
    m_arrow.onDrag = [this](double d) {
        m_distanceField->setExpression(QString::fromStdString(SketchEditor::formatExpression(d, cad::ValueKind::Length)));
        emit inputsChanged();
    };
    SelectFilter sf;
    sf.edges = sf.vertices = sf.bodies = sf.profiles = false;
    sf.planes = true;
    sf.planarFacesOnly = true;
    m_ctx.view->setFilter(sf);
    m_ctx.view->setOriginForced(true);
    m_planeField->setActive(true);
    updateArrow();
    if(m_plane) {
        m_distanceField->setFocus(Qt::OtherFocusReason);
        m_distanceField->selectAll();
    }
}

std::optional<cad::SectionAnalysis> SectionCommand::current() const {
    const std::optional<cad::PlaneRef> ref = m_plane ? m_plane->planeRef() : std::nullopt;
    if(!ref || !m_distanceField->valid()) return std::nullopt;
    cad::SectionAnalysis s;
    if(const cad::SectionAnalysis *old = m_ctx.doc->section(m_section)) s = *old;
    s.plane = *ref;
    s.offset = *m_distanceField->value();
    s.flip = m_flip->isChecked();
    s.visible = true;
    return s;
}

bool SectionCommand::ready(QString &why) {
    if(!m_plane) {
        why = tr("Select the plane to cut the model with.");
        return false;
    }
    if(!m_distanceField->valid()) {
        why = tr("Distance: enter a value");
        return false;
    }
    return true;
}

void SectionCommand::showPreview() {
    // Shown instead of the document's section while the dialog is open.
    m_ctx.view->setSectionOverride(current());
    ModelView::InputMarks marks;
    if(m_plane && m_ctx.view->state()) markInput(*m_ctx.view, *m_ctx.view->state(), *m_plane, -1, kInput, marks);
    m_ctx.view->setInputMarks(std::move(marks));
    m_planeField->setCount(m_plane ? 1 : 0);
    updateArrow();
}

void SectionCommand::apply() {
    const std::optional<cad::SectionAnalysis> s = current();
    if(!s) return;
    if(m_section) m_ctx.doc->updateSection(*s);
    else m_section = m_ctx.doc->addSection(*s);
}

void SectionCommand::end() { m_ctx.view->setSectionOverride(std::nullopt); }

void SectionCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    const std::optional<cad::PlaneRef> p = m_ctx.view->planeRefOf(*item);
    if(!p) return;
    m_plane = InputRef::ofPlane(*p);
    emit inputsChanged();
}

bool SectionCommand::arrowAxis(const cad::ModelState &st, const cad::PlaneRef &ref, QVector3D &origin,
                               QVector3D &direction) {
    gp_Ax3 frame;
    cad::Status status;
    if(!cad::resolvePlane(st, ref, frame, status)) return false;
    gp_Pnt c = frame.Location();
    if(ref.kind == cad::PlaneRef::Kind::Face) {
        const cad::ResolvedRef r = cad::resolveRef(st, ref.face);
        if(r.ok) {
            GProp_GProps g;
            BRepGProp::SurfaceProperties(r.shape, g);
            c = g.CentreOfMass();
        }
    } else if(ref.kind == cad::PlaneRef::Kind::Construction) {
        if(auto it = st.planes.find(ref.plane); it != st.planes.end()) c = it->second->center;
    } else {
        // The middle of the model, on the origin plane.
        QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
        for(const auto &kv : st.bodies)
            if(const auto m = kv.second->meshIfReady()) {
                lo = QVector3D(std::min(lo.x(), m->bboxMin[0]), std::min(lo.y(), m->bboxMin[1]), std::min(lo.z(), m->bboxMin[2]));
                hi = QVector3D(std::max(hi.x(), m->bboxMax[0]), std::max(hi.y(), m->bboxMax[1]), std::max(hi.z(), m->bboxMax[2]));
            }
        if(lo.x() <= hi.x()) {
            const QVector3D mid = (lo + hi) * 0.5f;
            const gp_Pnt m(mid.x(), mid.y(), mid.z());
            const gp_Vec off(frame.Location(), m);
            c = m.Translated(-gp_Vec(frame.Direction()) * off.Dot(gp_Vec(frame.Direction())));
        }
    }
    origin = toQ(c.XYZ());
    direction = toQ(frame.Direction().XYZ());
    return true;
}

void SectionCommand::updateArrow() {
    const cad::StatePtr st = m_ctx.view->state();
    const std::optional<cad::PlaneRef> ref = m_plane ? m_plane->planeRef() : std::nullopt;
    QVector3D o, d;
    const bool show = st && ref && arrowAxis(*st, *ref, o, d);
    m_arrow.setVisible(show);
    if(show) {
        m_arrow.setAxis(o, d);
        m_arrow.setDistance(m_distanceField->value().value_or(0.0));
        m_arrow.label = m_distanceField->expression();
    }
    m_ctx.viewport->refreshOverlay();
}

} // namespace cadly
