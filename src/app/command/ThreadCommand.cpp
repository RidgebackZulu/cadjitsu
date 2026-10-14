#include "command/ThreadCommand.h"

#include "model/ModelView.h"
#include "ui/Icons.h"

#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Surface.hxx>
#include <TopoDS.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>

#include <cmath>

namespace cadjitsu {

namespace {
const QColor kFace(230, 140, 40);
const QColor kBusy(255, 205, 0); // computing
} // namespace

IconId ThreadCommand::iconId() const { return IconId::Thread; }

void ThreadCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::ThreadFeature>(doc.feature(m_editing));
    const cad::ThreadFeature *o = m_original.get();
    m_facesField = panel.addSelection(tr("Faces"), tr("Select round faces"), "threadFaces");
    m_size = panel.addChoice(tr("Size"), {}, "threadSize");
    fillThreadSizes(m_size, tr("From the diameter"));
    m_detected = panel.addInfo(QString(), "threadDetected");
    m_detected->setStyleSheet(QStringLiteral("color: #3c4450; font-weight: normal;"));
    m_clearanceField = panel.addValue(tr("Clearance"), cad::ValueKind::Length, evaluator(), "threadClearance");
    m_full = panel.addCheck(tr("Full Length"), "threadFull");
    m_lengthField = panel.addValue(tr("Length"), cad::ValueKind::Length, evaluator(), "threadLength");
    m_left = panel.addCheck(tr("Left-Handed"), "threadLeft");
    m_csk.add(panel, evaluator(), "thread");
    m_clearance = o && !o->clearance.empty() ? o->clearance : doc.makeSlot("0.15 mm");
    m_length = o && !o->length.empty() ? o->length : doc.makeSlot("8 mm");
    m_cskDiameter = o && !o->countersinkDiameter.empty() ? o->countersinkDiameter : doc.makeSlot("");
    m_clearanceField->setExpression(QString::fromStdString(m_clearance.expr));
    m_lengthField->setExpression(QString::fromStdString(m_length.expr));
    m_full->setChecked(!o || o->length.empty());
    if(o) {
        for(const auto &f : o->faces) m_faces.push_back(InputRef::ofTopo(f));
        if(!o->size.empty()) m_size->setCurrentText(QString::fromStdString(o->size));
        m_left->setChecked(o->leftHand);
        m_csk.load(o->countersink, o->countersinkEnds, o->countersinkAngle, o->countersinkDiameter);
    } else {
        for(const auto &it : m_ctx.view->selection().items())
            if(it.kind == SelectionItem::Kind::Face)
                if(auto r = inputRefOf(*m_ctx.view, it)) m_faces.push_back(*r);
    }
    for(ValueField *v : {m_clearanceField, m_lengthField}) connect(v, &ValueField::edited, this, &Command::inputsChanged);
    connect(m_clearanceField, &ValueField::edited, this, &ThreadCommand::updateDetected);
    for(QComboBox *c : {m_size, m_csk.endsBox(), m_csk.angleBox()})
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            updateRows();
            emit inputsChanged();
        });
    connect(m_csk.diameterField(), &ValueField::edited, this, &Command::inputsChanged);
    for(QCheckBox *c : {m_full, m_left, m_csk.onBox()})
        connect(c, &QCheckBox::toggled, this, [this] {
            updateRows();
            emit inputsChanged();
        });
    connect(m_facesField, &SelectionField::cleared, this, [this] {
        m_faces.clear();
        updateMarks();
        emit inputsChanged();
    });
    SelectFilter sf;
    sf.vertices = sf.edges = sf.bodies = sf.profiles = false;
    m_ctx.view->setFilter(sf);
    m_facesField->setActive(true);
    updateRows();
    updateMarks();
}

void ThreadCommand::updateDetected() {
    // What the first face is, and the size that fits it.
    QString text;
    bool hole = true;
    const cad::StatePtr base = baseState();
    if(base && !m_faces.empty()) {
        const cad::ResolvedRef r = cad::resolveRef(*base, m_faces.front().topo);
        if(r.ok) {
            BRepAdaptor_Surface s(TopoDS::Face(r.shape));
            if(s.GetType() == GeomAbs_Cylinder) {
                const double d = 2 * s.Cylinder().Radius();
                gp_Dir n;
                const gp_Pnt mid = s.Value((s.FirstUParameter() + s.LastUParameter()) / 2,
                                           (s.FirstVParameter() + s.LastVParameter()) / 2);
                const gp_Ax1 ax = s.Cylinder().Axis();
                const gp_Pnt foot = ax.Location().Translated(gp_Vec(ax.Direction()) *
                                                              gp_Vec(ax.Location(), mid).Dot(gp_Vec(ax.Direction())));
                const bool internal = cad::faceNormalAt(TopoDS::Face(r.shape), mid, n) && gp_Vec(foot, mid).Dot(gp_Vec(n)) < 0;
                const cad::ThreadSpec *spec = m_size->currentIndex() > 0
                                                  ? cad::findThread(m_size->currentText().toStdString())
                                                  : cad::nearestThread(d, internal);
                hole = internal;
                text = internal ? tr("A %1 mm hole").arg(d, 0, 'f', 2) : tr("A %1 mm post").arg(d, 0, 'f', 2);
                if(spec) {
                    // It is fitted to the thread: filled and bored, grown or turned down to it.
                    const double clear = m_clearanceField->value().value_or(0.15);
                    const double fit = internal ? spec->minor() + 2 * clear : spec->major - 2 * clear;
                    const QString name = QString::fromStdString(spec->name);
                    text += std::fabs(d - fit) < 0.005 ? tr(": %1").arg(name)
                            : d > fit ? (internal ? tr(", filled and bored to %1").arg(name)
                                                  : tr(", turned down to %1").arg(name))
                                      : (internal ? tr(", opened to %1").arg(name) : tr(", grown to %1").arg(name));
                    text += tr(" (pitch %1 mm)").arg(spec->pitch, 0, 'g', 3);
                    m_csk.setAutoDiameter(spec->major + 2 * clear + 0.3 * spec->pitch);
                } else {
                    text += tr(": no standard size near it; choose one");
                }
            } else {
                text = tr("Not a round face");
            }
        }
    }
    m_detected->setText(text);
    m_ctx.panel->setRowVisible(m_detected, !text.isEmpty());
    // The countersink is a hole's.
    m_csk.setVisible(*m_ctx.panel, hole);
}

void ThreadCommand::updateRows() {
    CommandPanel &panel = *m_ctx.panel;
    panel.setRowVisible(m_lengthField, !m_full->isChecked());
    updateDetected();
}

void ThreadCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState())
        for(size_t i = 0; i < m_faces.size(); ++i) markInput(*m_ctx.view, *base, m_faces[i], int(i), m_busy ? kBusy : kFace, marks);
    m_ctx.view->setInputMarks(std::move(marks));
    m_facesField->setCount(int(m_faces.size()));
    updateDetected();
}

void ThreadCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item || item->kind != SelectionItem::Kind::Face) return;
    const cad::StatePtr st = m_ctx.view->state();
    const cad::Body *b = st ? st->body(item->body) : nullptr;
    if(!b || BRepAdaptor_Surface(b->shape.face(item->index)).GetType() != GeomAbs_Cylinder) {
        m_ctx.panel->setMessage(tr("Pick a round face (a hole's wall or a post)."), cad::Severity::Warning);
        return;
    }
    const auto r = inputRefOf(*m_ctx.view, *item);
    if(!r) return;
    toggleRef(m_faces, *r);
    updateMarks();
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> ThreadCommand::build(QString &why) {
    if(m_faces.empty()) {
        why = tr("Click the round faces of holes or posts to thread.");
        return nullptr;
    }
    if(!m_clearanceField->valid() || (!m_full->isChecked() && !m_lengthField->valid())) {
        why = tr("Enter the clearance and length.");
        return nullptr;
    }
    if(!m_csk.valid()) {
        why = tr("Enter the countersink diameter, or leave it blank.");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::ThreadFeature>(m_original->clone())
                        : std::make_shared<cad::ThreadFeature>();
    f->faces.clear();
    for(const auto &r : m_faces) f->faces.push_back(r.topo);
    f->size = m_size->currentIndex() > 0 ? m_size->currentText().toStdString() : std::string();
    f->clearance = {m_clearance.name, m_clearanceField->expression().toStdString()};
    f->length = m_full->isChecked() ? cad::ParamSlot() : cad::ParamSlot{m_length.name, m_lengthField->expression().toStdString()};
    f->leftHand = m_left->isChecked();
    f->countersink = m_csk.on();
    f->countersinkEnds = m_csk.ends();
    f->countersinkAngle = m_csk.angle();
    f->countersinkDiameter = m_csk.diameter(m_cskDiameter);
    return f;
}

void ThreadCommand::accepted(const cad::Feature &f) {
    noteThreadSize(QString::fromStdString(static_cast<const cad::ThreadFeature &>(f).size));
}

void ThreadCommand::previewBusy(bool busy) {
    m_busy = busy;
    updateMarks();
}

} // namespace cadjitsu
