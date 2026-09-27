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

namespace cadly {

namespace {
const QColor kFace(230, 140, 40);
const cad::ThreadMode kModes[] = {cad::ThreadMode::Auto, cad::ThreadMode::Modeled, cad::ThreadMode::TapDrill};
} // namespace

IconId ThreadCommand::iconId() const { return IconId::Thread; }

void ThreadCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::ThreadFeature>(doc.feature(m_editing));
    const cad::ThreadFeature *o = m_original.get();
    m_facesField = panel.addSelection(tr("Faces"), tr("Select round faces"), "threadFaces");
    QStringList sizes{tr("From the diameter")};
    for(const cad::ThreadSpec &t : cad::threadTable()) sizes << QString::fromStdString(t.name);
    m_size = panel.addChoice(tr("Size"), sizes, "threadSize");
    m_detected = panel.addInfo(QString(), "threadDetected");
    m_detected->setStyleSheet(QStringLiteral("color: #3c4450; font-weight: normal;"));
    m_mode = panel.addChoice(tr("Thread Type"), {tr("By size (M5 up modelled)"), tr("Modelled"), tr("Tap drill / plain")},
                             "threadMode");
    m_mode->setToolTip(tr("Small threads print poorly: by default M4 / #8 and smaller holes are only opened to the tap "
                          "drill (tap them, or use a self-tapping screw or a heat-set insert)."));
    m_clearanceField = panel.addValue(tr("Clearance"), cad::ValueKind::Length, evaluator(), "threadClearance");
    m_full = panel.addCheck(tr("Full Length"), "threadFull");
    m_lengthField = panel.addValue(tr("Length"), cad::ValueKind::Length, evaluator(), "threadLength");
    m_left = panel.addCheck(tr("Left-Handed"), "threadLeft");
    m_clearance = o && !o->clearance.empty() ? o->clearance : doc.makeSlot("0.15 mm");
    m_length = o && !o->length.empty() ? o->length : doc.makeSlot("8 mm");
    m_clearanceField->setExpression(QString::fromStdString(m_clearance.expr));
    m_lengthField->setExpression(QString::fromStdString(m_length.expr));
    m_full->setChecked(!o || o->length.empty());
    if(o) {
        for(const auto &f : o->faces) m_faces.push_back(InputRef::ofTopo(f));
        if(!o->size.empty()) m_size->setCurrentText(QString::fromStdString(o->size));
        for(int i = 0; i < 3; ++i)
            if(kModes[i] == o->mode) m_mode->setCurrentIndex(i);
        m_left->setChecked(o->leftHand);
    } else {
        for(const auto &it : m_ctx.view->selection().items())
            if(it.kind == SelectionItem::Kind::Face)
                if(auto r = inputRefOf(*m_ctx.view, it)) m_faces.push_back(*r);
    }
    for(ValueField *v : {m_clearanceField, m_lengthField}) connect(v, &ValueField::edited, this, &Command::inputsChanged);
    for(QComboBox *c : {m_size, m_mode})
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            updateRows();
            emit inputsChanged();
        });
    for(QCheckBox *c : {m_full, m_left})
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
                text = internal ? tr("A %1 mm hole").arg(d, 0, 'f', 2) : tr("A %1 mm boss").arg(d, 0, 'f', 2);
                if(spec)
                    text += tr(": %1 (pitch %2 mm, tap drill %3 mm)")
                                .arg(QString::fromStdString(spec->name))
                                .arg(spec->pitch, 0, 'g', 3)
                                .arg(spec->tapDrill, 0, 'f', 2);
                else
                    text += tr(": no standard size fits; choose one");
            } else {
                text = tr("Not a round face");
            }
        }
    }
    m_detected->setText(text);
    m_ctx.panel->setRowVisible(m_detected, !text.isEmpty());
}

void ThreadCommand::updateRows() {
    CommandPanel &panel = *m_ctx.panel;
    panel.setRowVisible(m_lengthField, !m_full->isChecked());
    panel.setRowVisible(m_clearanceField, m_mode->currentIndex() != 2);
    updateDetected();
}

void ThreadCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState())
        for(size_t i = 0; i < m_faces.size(); ++i) markInput(*m_ctx.view, *base, m_faces[i], int(i), kFace, marks);
    m_ctx.view->setInputMarks(std::move(marks));
    m_facesField->setCount(int(m_faces.size()));
    updateDetected();
}

void ThreadCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item || item->kind != SelectionItem::Kind::Face) return;
    const cad::StatePtr st = m_ctx.view->state();
    const cad::Body *b = st ? st->body(item->body) : nullptr;
    if(!b || BRepAdaptor_Surface(b->shape.face(item->index)).GetType() != GeomAbs_Cylinder) {
        m_ctx.panel->setMessage(tr("Pick a round face (a hole's wall or a boss)."), cad::Severity::Warning);
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
        why = tr("Click the round faces of holes or bosses to thread.");
        return nullptr;
    }
    if(!m_clearanceField->valid() || (!m_full->isChecked() && !m_lengthField->valid())) {
        why = tr("Enter the clearance and length.");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::ThreadFeature>(m_original->clone())
                        : std::make_shared<cad::ThreadFeature>();
    f->faces.clear();
    for(const auto &r : m_faces) f->faces.push_back(r.topo);
    f->size = m_size->currentIndex() > 0 ? m_size->currentText().toStdString() : std::string();
    f->mode = kModes[std::clamp(m_mode->currentIndex(), 0, 2)];
    f->clearance = {m_clearance.name, m_clearanceField->expression().toStdString()};
    f->length = m_full->isChecked() ? cad::ParamSlot() : cad::ParamSlot{m_length.name, m_lengthField->expression().toStdString()};
    f->leftHand = m_left->isChecked();
    return f;
}

} // namespace cadly
