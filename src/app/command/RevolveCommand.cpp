#include "command/RevolveCommand.h"

#include "model/ModelView.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include "topo/Resolver.h"

#include <QCheckBox>
#include <QComboBox>

namespace cadjitsu {

namespace {

const cad::BodyOperation kOperations[] = {cad::BodyOperation::NewBody, cad::BodyOperation::Join,
                                          cad::BodyOperation::Cut, cad::BodyOperation::Intersect};
const cad::RevolveExtent kExtents[] = {cad::RevolveExtent::OneSide, cad::RevolveExtent::Symmetric,
                                       cad::RevolveExtent::TwoSides};
const char *const kBuiltin[] = {"", "x", "y", "z"};

const QColor kActive(20, 100, 225);
const QColor kOther(120, 150, 200);
const QColor kAxis(229, 70, 58);

template <class T, size_t N> int indexIn(const T (&list)[N], T v) {
    for(size_t i = 0; i < N; ++i)
        if(list[i] == v) return int(i);
    return 0;
}

} // namespace

RevolveCommand::RevolveCommand(const CommandContext &ctx, cad::FeatureId editing) : Command(ctx, editing) {}

IconId RevolveCommand::iconId() const { return IconId::Revolve; }

cad::BodyOperation RevolveCommand::operation() const {
    return kOperations[std::clamp(m_operation->currentIndex(), 0, 3)];
}

bool RevolveCommand::hasAxis() const {
    return m_axisKind->currentIndex() > 0 || m_axisRef || m_axisSketch != cad::kNoFeature;
}

void RevolveCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::RevolveFeature>(doc.feature(m_editing));
    m_profilesField = panel.addSelection(tr("Profiles"), tr("Select profiles or faces"), "revolveProfiles");
    m_axisKind = panel.addChoice(tr("Axis"), {tr("Line or edge"), tr("X axis"), tr("Y axis"), tr("Z axis")},
                                 "revolveAxisKind");
    m_axisField = panel.addSelection(tr("Axis Line"), tr("Select a line or edge"), "revolveAxis");
    m_extent = panel.addChoice(tr("Type"), {tr("One Side"), tr("Symmetric"), tr("Two Sides")}, "revolveExtent");
    m_angleField = panel.addAngle(tr("Angle"), evaluator(), "revolveAngle", kAxis);
    m_angle2Field = panel.addAngle(tr("Angle 2"), evaluator(), "revolveAngle2", QColor(40, 150, 70));
    m_flip = panel.addCheck(tr("Reverse"), "revolveFlip");
    m_operation = panel.addChoice(tr("Operation"), {tr("New Body"), tr("Join"), tr("Cut"), tr("Intersect")},
                                  "revolveOperation");

    m_angle = m_original && !m_original->angle.empty() ? m_original->angle : doc.makeSlot("360 deg");
    m_angle2 = m_original && !m_original->angle2.empty() ? m_original->angle2 : doc.makeSlot("90 deg");
    m_angleField->setExpression(QString::fromStdString(m_angle.expr));
    m_angle2Field->setExpression(QString::fromStdString(m_angle2.expr));
    if(m_original) {
        for(const auto &p : m_original->profiles) m_profiles.push_back(InputRef::ofProfile(p));
        for(const auto &f : m_original->faces) m_profiles.push_back(InputRef::ofTopo(f));
        const cad::RevolveAxis &a = m_original->axis;
        if(a.sketch) {
            m_axisSketch = a.sketch;
            m_axisLine = a.line;
        } else if(!a.axis.builtin.empty()) {
            for(int i = 1; i < 4; ++i)
                if(a.axis.builtin == kBuiltin[i]) m_axisKind->setCurrentIndex(i);
        } else if(!a.axis.ref.empty()) {
            m_axisRef = InputRef::ofTopo(a.axis.ref);
        }
        m_extent->setCurrentIndex(indexIn(kExtents, m_original->extent));
        m_flip->setChecked(m_original->flip);
        m_operation->setCurrentIndex(indexIn(kOperations, m_original->operation));
    } else {
        for(const auto &it : m_ctx.view->selection().items()) {
            if(it.kind == SelectionItem::Kind::Profile) {
                if(auto r = inputRefOf(*m_ctx.view, it)) m_profiles.push_back(*r);
            } else if(it.kind == SelectionItem::Kind::SketchEntity) {
                m_axisSketch = it.feature;
                m_axisLine = it.entity;
            }
        }
    }
    for(ValueField *v : {m_angleField, m_angle2Field})
        connect(v, &ValueField::edited, this, &RevolveCommand::inputsChanged);
    for(QComboBox *c : {m_extent, m_operation})
        connect(c, &QComboBox::activated, this, [this] {
            updateRows();
            emit inputsChanged();
        });
    connect(m_axisKind, &QComboBox::activated, this, [this] {
        updateRows();
        if(m_axisKind->currentIndex() == 0) activate(Field::Axis);
        else activate(Field::Profiles);
        emit inputsChanged();
    });
    connect(m_flip, &QCheckBox::toggled, this, &RevolveCommand::inputsChanged);
    connect(m_profilesField, &SelectionField::activated, this, [this] { activate(Field::Profiles); });
    connect(m_axisField, &SelectionField::activated, this, [this] { activate(Field::Axis); });
    connect(m_profilesField, &SelectionField::cleared, this, [this] {
        m_profiles.clear();
        activate(Field::Profiles);
        emit inputsChanged();
    });
    connect(m_axisField, &SelectionField::cleared, this, [this] {
        m_axisRef.reset();
        m_axisSketch = cad::kNoFeature;
        m_axisLine = 0;
        activate(Field::Axis);
        emit inputsChanged();
    });
    updateRows();
    activate(m_profiles.empty() || hasAxis() ? Field::Profiles : Field::Axis);
}

void RevolveCommand::updateRows() {
    CommandPanel &panel = *m_ctx.panel;
    panel.setRowVisible(m_axisField, m_axisKind->currentIndex() == 0);
    panel.setRowVisible(m_angle2Field, kExtents[m_extent->currentIndex()] == cad::RevolveExtent::TwoSides);
    panel.setRowLabel(m_angleField, kExtents[m_extent->currentIndex()] == cad::RevolveExtent::Symmetric
                                        ? tr("Angle (each side)")
                                        : tr("Angle"));
}

void RevolveCommand::activate(Field f) {
    m_active = f;
    m_profilesField->setActive(f == Field::Profiles);
    m_axisField->setActive(f == Field::Axis);
    SelectFilter sf;
    sf.vertices = sf.bodies = false;
    if(f == Field::Profiles) {
        sf.edges = false;
        sf.planarFacesOnly = true;
    } else {
        sf.profiles = false;
        sf.sketchCurves = true;
    }
    m_ctx.view->setFilter(sf);
    updateMarks();
}

void RevolveCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState()) {
        for(size_t i = 0; i < m_profiles.size(); ++i)
            markInput(*m_ctx.view, *base, m_profiles[i], int(i), m_active == Field::Profiles ? kActive : kOther, marks);
        if(m_axisRef) markInput(*m_ctx.view, *base, *m_axisRef, 999, kAxis, marks);
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_profilesField->setCount(int(m_profiles.size()));
    const bool picked = m_axisRef || m_axisSketch != cad::kNoFeature;
    m_axisField->setCount(picked ? 1 : 0);
    m_axisField->setDetail(m_axisSketch != cad::kNoFeature ? tr("Sketch line") : m_axisRef ? tr("Edge") : QString());
}

void RevolveCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    if(m_active == Field::Axis) {
        if(item->kind == SelectionItem::Kind::SketchEntity) {
            const cad::StatePtr base = baseState();
            if(!base || !base->sketches.count(item->feature)) return;
            const cad::SkEntity *e = base->sketches.at(item->feature)->sketch.find(item->entity);
            if(!e || e->type != cad::SkType::Line) return;
            m_axisRef.reset();
            m_axisSketch = item->feature;
            m_axisLine = item->entity;
        } else if(item->kind == SelectionItem::Kind::Edge || item->kind == SelectionItem::Kind::Face) {
            const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
            if(!r || r->createdBy(isEditing() ? m_editing : m_ctx.doc->nextFeatureId())) return;
            m_axisRef = *r;
            m_axisSketch = cad::kNoFeature;
            m_axisLine = 0;
        } else {
            return;
        }
        m_axisKind->setCurrentIndex(0);
        activate(Field::Profiles);
        emit inputsChanged();
        return;
    }
    const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
    if(!r || r->createdBy(isEditing() ? m_editing : m_ctx.doc->nextFeatureId())) return;
    const bool face = item->kind == SelectionItem::Kind::Face && m_ctx.view->planeRefOf(*item).has_value();
    if(r->kind != SelectionItem::Kind::Profile && !face) return;
    toggleRef(m_profiles, *r);
    // The axis comes next.
    if(m_profiles.size() == 1 && !hasAxis()) {
        activate(Field::Axis);
    } else {
        updateMarks();
    }
    emit inputsChanged();
}

void RevolveCommand::markClicked(int tag) {
    if(tag == 999) {
        m_axisRef.reset();
    } else if(tag >= 0 && tag < int(m_profiles.size())) {
        m_profiles.erase(m_profiles.begin() + tag);
    }
    updateMarks();
    emit inputsChanged();
}

std::set<cad::FeatureId> RevolveCommand::sketchesToShow() const {
    std::set<cad::FeatureId> out;
    for(const auto &r : m_profiles)
        if(r.kind == SelectionItem::Kind::Profile) out.insert(r.profile.sketch);
    if(m_axisSketch != cad::kNoFeature) out.insert(m_axisSketch);
    return out;
}

std::shared_ptr<cad::Feature> RevolveCommand::build(QString &why) {
    if(m_profiles.empty()) {
        why = tr("Select the profiles to revolve.");
        return nullptr;
    }
    if(!hasAxis()) {
        why = tr("Select the axis: a sketch line or an edge, or choose X, Y or Z.");
        return nullptr;
    }
    const cad::RevolveExtent extent = kExtents[m_extent->currentIndex()];
    if(!m_angleField->valid() || (extent == cad::RevolveExtent::TwoSides && !m_angle2Field->valid())) {
        why = tr("Angle: enter a value");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::RevolveFeature>(m_original->clone())
                        : std::make_shared<cad::RevolveFeature>();
    f->profiles.clear();
    f->faces.clear();
    for(const auto &r : m_profiles) {
        if(r.kind == SelectionItem::Kind::Profile) {
            cad::ProfileRef p = r.profile;
            if(const cad::StatePtr base = baseState()) cad::captureProfileOutline(*base, p);
            f->profiles.push_back(p);
        } else {
            f->faces.push_back(r.topo);
        }
    }
    f->axis = {};
    if(m_axisKind->currentIndex() > 0) {
        f->axis.axis.builtin = kBuiltin[m_axisKind->currentIndex()];
    } else if(m_axisSketch != cad::kNoFeature) {
        f->axis.sketch = m_axisSketch;
        f->axis.line = m_axisLine;
    } else if(m_axisRef) {
        f->axis.axis.ref = m_axisRef->topo;
    }
    f->extent = extent;
    f->angle = {m_angle.name, m_angleField->expression().toStdString()};
    f->angle2 = extent == cad::RevolveExtent::TwoSides
                    ? cad::ParamSlot{m_angle2.name, m_angle2Field->expression().toStdString()}
                    : cad::ParamSlot{};
    f->flip = m_flip->isChecked();
    f->operation = operation();
    f->participants.clear();
    return f;
}

} // namespace cadjitsu
