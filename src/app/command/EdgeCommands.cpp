#include "command/EdgeCommands.h"

#include "model/ModelView.h"
#include "ui/Icons.h"

#include <QCheckBox>
#include <QComboBox>

namespace cadjitsu {

namespace {

const QColor kInput(20, 100, 225);

const cad::ChamferType kChamferTypes[] = {cad::ChamferType::EqualDistance, cad::ChamferType::TwoDistances,
                                          cad::ChamferType::DistanceAngle};

} // namespace

// --- edges input --------------------------------------------------------------------

void EdgesCommand::setupEdges(const std::vector<cad::TopoRef> &edges, const std::vector<cad::TopoRef> &faces) {
    m_edgesField = m_ctx.panel->addSelection(tr("Edges"), tr("Select edges or faces"), "edges");
    if(isEditing()) {
        for(const auto &e : edges) m_refs.push_back(InputRef::ofTopo(e));
        for(const auto &f : faces) m_refs.push_back(InputRef::ofTopo(f));
    } else {
        // What is selected when the command starts, as in Fusion.
        for(const auto &it : m_ctx.view->selection().items())
            if(it.kind == SelectionItem::Kind::Edge || it.kind == SelectionItem::Kind::Face)
                if(const auto r = inputRefOf(*m_ctx.view, it)) m_refs.push_back(*r);
    }
    SelectFilter sf;
    sf.vertices = sf.bodies = sf.profiles = false;
    m_ctx.view->setFilter(sf);
    m_edgesField->setActive(true);
    connect(m_edgesField, &SelectionField::cleared, this, [this] {
        m_refs.clear();
        updateMarks();
        emit inputsChanged();
    });
    updateMarks();
}

void EdgesCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState())
        for(size_t i = 0; i < m_refs.size(); ++i) markInput(*m_ctx.view, *base, m_refs[i], int(i), kInput, marks);
    m_ctx.view->setInputMarks(std::move(marks));
    m_edgesField->setCount(int(m_refs.size()));
}

void EdgesCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item || (item->kind != SelectionItem::Kind::Edge && item->kind != SelectionItem::Kind::Face)) return;
    const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
    // Edges and faces the preview made are not inputs.
    if(!r || r->createdBy(isEditing() ? m_editing : m_ctx.doc->nextFeatureId())) return;
    toggleRef(m_refs, *r);
    updateMarks();
    emit inputsChanged();
}

void EdgesCommand::markClicked(int tag) {
    if(tag < 0 || tag >= int(m_refs.size())) return;
    m_refs.erase(m_refs.begin() + tag);
    updateMarks();
    emit inputsChanged();
}

void EdgesCommand::splitRefs(std::vector<cad::TopoRef> &edges, std::vector<cad::TopoRef> &faces) const {
    edges.clear();
    faces.clear();
    for(const auto &r : m_refs) (r.kind == SelectionItem::Kind::Edge ? edges : faces).push_back(r.topo);
}

bool EdgesCommand::needEdges(QString &why, const QString &message) const {
    if(!m_refs.empty()) return true;
    why = message;
    return false;
}

// --- fillet -------------------------------------------------------------------------

IconId FilletCommand::iconId() const { return IconId::Fillet; }

void FilletCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::FilletFeature>(doc.feature(m_editing));
    setupEdges(m_original ? m_original->edges : std::vector<cad::TopoRef>{},
               m_original ? m_original->faces : std::vector<cad::TopoRef>{});
    m_radius = m_original && !m_original->radius.empty() ? m_original->radius : doc.makeSlot("1 mm");
    m_radiusField = m_ctx.panel->addValue(tr("Radius"), cad::ValueKind::Length, evaluator(), "filletRadius");
    m_radiusField->setExpression(QString::fromStdString(m_radius.expr));
    connect(m_radiusField, &ValueField::edited, this, &Command::inputsChanged);
    if(!m_refs.empty()) {
        m_radiusField->setFocus(Qt::OtherFocusReason);
        m_radiusField->selectAll();
    }
}

std::shared_ptr<cad::Feature> FilletCommand::build(QString &why) {
    if(!needEdges(why, tr("Select the edges or faces to round."))) return nullptr;
    if(!m_radiusField->valid()) {
        why = tr("Radius: enter a value");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::FilletFeature>(m_original->clone())
                        : std::make_shared<cad::FilletFeature>();
    splitRefs(f->edges, f->faces);
    f->radius = {m_radius.name, m_radiusField->expression().toStdString()};
    return f;
}

// --- chamfer ------------------------------------------------------------------------

IconId ChamferCommand::iconId() const { return IconId::Chamfer; }

void ChamferCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::ChamferFeature>(doc.feature(m_editing));
    setupEdges(m_original ? m_original->edges : std::vector<cad::TopoRef>{},
               m_original ? m_original->faces : std::vector<cad::TopoRef>{});
    m_type = panel.addChoice(tr("Chamfer Type"), {tr("Equal Distance"), tr("Two Distances"), tr("Distance and Angle")},
                             "chamferType");
    m_distanceField = panel.addValue(tr("Distance"), cad::ValueKind::Length, evaluator(), "chamferDistance");
    m_distance2Field = panel.addValue(tr("Distance 2"), cad::ValueKind::Length, evaluator(), "chamferDistance2");
    m_angleField = panel.addValue(tr("Angle"), cad::ValueKind::Angle, evaluator(), "chamferAngle");
    m_flip = panel.addCheck(tr("Flip"), "chamferFlip");
    auto slot = [&](const cad::ParamSlot &s, const char *fallback) {
        return s.empty() ? doc.makeSlot(fallback) : s;
    };
    m_distance = slot(m_original ? m_original->distance : cad::ParamSlot{}, "1 mm");
    m_distance2 = slot(m_original ? m_original->distance2 : cad::ParamSlot{}, "1 mm");
    m_angle = slot(m_original ? m_original->angle : cad::ParamSlot{}, "45 deg");
    m_distanceField->setExpression(QString::fromStdString(m_distance.expr));
    m_distance2Field->setExpression(QString::fromStdString(m_distance2.expr));
    m_angleField->setExpression(QString::fromStdString(m_angle.expr));
    if(m_original) {
        for(int i = 0; i < 3; ++i)
            if(kChamferTypes[i] == m_original->chamferType) m_type->setCurrentIndex(i);
        m_flip->setChecked(m_original->flip);
    }
    connect(m_type, &QComboBox::currentIndexChanged, this, [this] {
        updateRows();
        emit inputsChanged();
    });
    connect(m_flip, &QCheckBox::toggled, this, &Command::inputsChanged);
    for(ValueField *v : {m_distanceField, m_distance2Field, m_angleField})
        connect(v, &ValueField::edited, this, &Command::inputsChanged);
    updateRows();
    if(!m_refs.empty()) {
        m_distanceField->setFocus(Qt::OtherFocusReason);
        m_distanceField->selectAll();
    }
}

void ChamferCommand::updateRows() {
    const cad::ChamferType t = kChamferTypes[std::clamp(m_type->currentIndex(), 0, 2)];
    m_ctx.panel->setRowVisible(m_distance2Field, t == cad::ChamferType::TwoDistances);
    m_ctx.panel->setRowVisible(m_angleField, t == cad::ChamferType::DistanceAngle);
    m_ctx.panel->setRowVisible(m_flip, t != cad::ChamferType::EqualDistance);
}

std::shared_ptr<cad::Feature> ChamferCommand::build(QString &why) {
    if(!needEdges(why, tr("Select the edges or faces to bevel."))) return nullptr;
    const cad::ChamferType t = kChamferTypes[std::clamp(m_type->currentIndex(), 0, 2)];
    auto bad = [&](ValueField *v, const QString &what) {
        if(v->valid()) return false;
        why = tr("%1: enter a value").arg(what);
        return true;
    };
    if(bad(m_distanceField, tr("Distance"))) return nullptr;
    if(t == cad::ChamferType::TwoDistances && bad(m_distance2Field, tr("Distance 2"))) return nullptr;
    if(t == cad::ChamferType::DistanceAngle && bad(m_angleField, tr("Angle"))) return nullptr;
    auto f = m_original ? std::static_pointer_cast<cad::ChamferFeature>(m_original->clone())
                        : std::make_shared<cad::ChamferFeature>();
    splitRefs(f->edges, f->faces);
    f->chamferType = t;
    f->distance = {m_distance.name, m_distanceField->expression().toStdString()};
    f->distance2 = t == cad::ChamferType::TwoDistances
                       ? cad::ParamSlot{m_distance2.name, m_distance2Field->expression().toStdString()}
                       : cad::ParamSlot{};
    f->angle = t == cad::ChamferType::DistanceAngle ? cad::ParamSlot{m_angle.name, m_angleField->expression().toStdString()}
                                                    : cad::ParamSlot{};
    f->flip = t != cad::ChamferType::EqualDistance && m_flip->isChecked();
    return f;
}

} // namespace cadjitsu
