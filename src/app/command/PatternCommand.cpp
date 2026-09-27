#include "command/PatternCommand.h"

#include "model/ModelView.h"
#include "ui/AngleDial.h"
#include "ui/Icons.h"

#include "features/ExtrudeFeature.h"
#include "features/HoleFeature.h"

#include <QCheckBox>
#include <QComboBox>

#include <algorithm>
#include <regex>

namespace cadly {

namespace {
const QColor kObject(230, 140, 40);
const QColor kRef(20, 100, 225);
const char *kDirNames[] = {"x", "y", "z"};
const char *kAxisNames[] = {"z", "x", "y"};
} // namespace

QString PatternCommand::title() const {
    switch(m_kind) {
    case cad::PatternKind::Mirror: return tr("Mirror");
    case cad::PatternKind::Rectangular: return tr("Rectangular Pattern");
    case cad::PatternKind::Circular: return tr("Circular Pattern");
    }
    return {};
}

QString PatternCommand::prompt() const {
    switch(m_kind) {
    case cad::PatternKind::Mirror: return tr("Pick the bodies (or a face of each hole / extrude) and the mirror plane.");
    case cad::PatternKind::Rectangular:
        return tr("Pick the bodies (or a face of each hole / extrude), the direction, count and spacing.");
    case cad::PatternKind::Circular: return tr("Pick the bodies (or a face of each hole / extrude), the axis and the count.");
    }
    return {};
}

IconId PatternCommand::iconId() const {
    return m_kind == cad::PatternKind::Mirror     ? IconId::Mirror
           : m_kind == cad::PatternKind::Circular ? IconId::PatternCircular
                                                  : IconId::PatternRect;
}

void PatternCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) {
        m_original = std::dynamic_pointer_cast<const cad::PatternFeature>(doc.feature(m_editing));
        if(m_original) m_kind = m_original->kind;
    }
    const cad::PatternFeature *o = m_original.get();
    m_objects = panel.addChoice(tr("Objects"), {tr("Bodies"), tr("Features")}, "patternObjects");
    m_objectsField = panel.addSelection(tr("Bodies"), tr("Select bodies"), "patternTargets");
    auto slot = [&](const cad::ParamSlot *s, const char *fallback) {
        return s && !s->empty() ? *s : doc.makeSlot(fallback);
    };
    auto value = [&](const QString &label, cad::ValueKind k, const char *name, const cad::ParamSlot &s) {
        ValueField *f = panel.addValue(label, k, evaluator(), name);
        f->setExpression(QString::fromStdString(s.expr));
        connect(f, &ValueField::edited, this, &Command::inputsChanged);
        return f;
    };
    switch(m_kind) {
    case cad::PatternKind::Mirror:
        m_planeField = panel.addSelection(tr("Mirror Plane"), tr("Select a plane or face"), "patternPlane");
        break;
    case cad::PatternKind::Rectangular:
        m_dir1 = panel.addChoice(tr("Direction"), {tr("X"), tr("Y"), tr("Z"), tr("Along an edge")}, "patternDir1");
        m_dir1Field = panel.addSelection(tr("Edge"), tr("Select a straight edge"), "patternDir1Edge");
        m_count1Slot = slot(o ? &o->count1 : nullptr, "3");
        m_spacing1Slot = slot(o ? &o->spacing1 : nullptr, "20 mm");
        m_count1 = value(tr("Count"), cad::ValueKind::Scalar, "patternCount1", m_count1Slot);
        m_spacing1 = value(tr("Spacing"), cad::ValueKind::Length, "patternSpacing1", m_spacing1Slot);
        m_second = panel.addCheck(tr("Second Direction"), "patternSecond");
        m_dir2 = panel.addChoice(tr("Direction 2"), {tr("X"), tr("Y"), tr("Z"), tr("Along an edge")}, "patternDir2");
        m_dir2->setCurrentIndex(1);
        m_dir2Field = panel.addSelection(tr("Edge 2"), tr("Select a straight edge"), "patternDir2Edge");
        m_count2Slot = slot(o ? &o->count2 : nullptr, "2");
        m_spacing2Slot = slot(o ? &o->spacing2 : nullptr, "20 mm");
        m_count2 = value(tr("Count 2"), cad::ValueKind::Scalar, "patternCount2", m_count2Slot);
        m_spacing2 = value(tr("Spacing 2"), cad::ValueKind::Length, "patternSpacing2", m_spacing2Slot);
        break;
    case cad::PatternKind::Circular:
        m_axisBox = panel.addChoice(tr("Axis"), {tr("Z"), tr("X"), tr("Y"), tr("Edge or cylinder")}, "patternAxis");
        m_axisField = panel.addSelection(tr("Axis Edge"), tr("Select an edge or a cylinder"), "patternAxisRef");
        m_countSlot = slot(o ? &o->count : nullptr, "6");
        m_angleSlot = slot(o ? &o->angle : nullptr, "360 deg");
        m_count = value(tr("Count"), cad::ValueKind::Scalar, "patternCount", m_countSlot);
        m_angle = panel.addAngle(tr("Total Angle"), evaluator(), "patternAngle", QColor(229, 70, 58));
        panel.angleDial(m_angle)->setFullTurn(true);
        m_angle->setExpression(QString::fromStdString(m_angleSlot.expr));
        connect(m_angle, &ValueField::edited, this, &Command::inputsChanged);
        m_symmetric = panel.addCheck(tr("Symmetric"), "patternSymmetric");
        break;
    }
    m_join = panel.addCheck(tr("Join to Original"), "patternJoin");
    m_join->setChecked(true);

    auto builtinIndex = [](const cad::PatternAxis &a, const char *const names[]) {
        for(int i = 0; i < 3; ++i)
            if(a.builtin == names[i]) return i;
        return a.ref.empty() ? 0 : 3;
    };
    if(o) {
        m_objects->setCurrentIndex(o->features.empty() ? 0 : 1);
        for(const auto &b : o->bodies) m_bodies.push_back(InputRef::ofBody(b));
        m_features = o->features;
        m_join->setChecked(o->join);
        if(m_planeField) m_plane = InputRef::ofPlane(o->plane);
        if(m_dir1) {
            m_dir1->setCurrentIndex(builtinIndex(o->dir1, kDirNames));
            if(!o->dir1.ref.empty()) m_dir1Ref = InputRef::ofTopo(o->dir1.ref);
            m_second->setChecked(!o->dir2.empty());
            if(!o->dir2.empty()) m_dir2->setCurrentIndex(builtinIndex(o->dir2, kDirNames));
            if(!o->dir2.ref.empty()) m_dir2Ref = InputRef::ofTopo(o->dir2.ref);
        }
        if(m_axisBox) {
            m_axisBox->setCurrentIndex(builtinIndex(o->axis, kAxisNames));
            if(!o->axis.ref.empty()) m_axisRef = InputRef::ofTopo(o->axis.ref);
            m_symmetric->setChecked(o->symmetric);
        }
    } else {
        for(const auto &it : m_ctx.view->selection().items())
            if(it.kind == SelectionItem::Kind::Body) m_bodies.push_back(InputRef::ofBody(it.body));
    }
    for(QComboBox *c : {m_objects, m_dir1, m_dir2, m_axisBox})
        if(c)
            connect(c, &QComboBox::currentIndexChanged, this, [this, c] {
                updateRows();
                if(c == m_objects) activate(Field::Objects);
                else if(c == m_dir1 && m_dir1->currentIndex() == 3) activate(Field::Dir1);
                else if(c == m_dir2 && m_dir2->currentIndex() == 3) activate(Field::Dir2);
                else if(c == m_axisBox && m_axisBox->currentIndex() == 3) activate(Field::Axis);
                emit inputsChanged();
            });
    for(QCheckBox *c : {m_second, m_symmetric, m_join})
        if(c)
            connect(c, &QCheckBox::toggled, this, [this] {
                updateRows();
                emit inputsChanged();
            });
    const std::pair<SelectionField *, Field> fields[] = {{m_objectsField, Field::Objects}, {m_planeField, Field::Plane},
                                                         {m_dir1Field, Field::Dir1},       {m_dir2Field, Field::Dir2},
                                                         {m_axisField, Field::Axis}};
    for(const auto &[sf, f] : fields) {
        if(!sf) continue;
        const Field fld = f;
        connect(sf, &SelectionField::activated, this, [this, fld] { activate(fld); });
        connect(sf, &SelectionField::cleared, this, [this, fld] {
            switch(fld) {
            case Field::Objects:
                m_bodies.clear();
                m_features.clear();
                break;
            case Field::Plane: m_plane.reset(); break;
            case Field::Dir1: m_dir1Ref.reset(); break;
            case Field::Dir2: m_dir2Ref.reset(); break;
            case Field::Axis: m_axisRef.reset(); break;
            }
            activate(fld);
            emit inputsChanged();
        });
    }
    if(m_planeField) m_ctx.view->setOriginForced(true);
    updateRows();
    const bool haveObjects = !m_bodies.empty() || !m_features.empty();
    activate(haveObjects && m_planeField ? Field::Plane : Field::Objects);
}

bool PatternCommand::featuresMode() const { return m_objects->currentIndex() == 1; }

void PatternCommand::updateRows() {
    CommandPanel &panel = *m_ctx.panel;
    const bool features = featuresMode();
    panel.setRowLabel(m_objectsField, features ? tr("Features") : tr("Bodies"));
    panel.setRowVisible(m_join, !features);
    if(m_dir1) {
        panel.setRowVisible(m_dir1Field, m_dir1->currentIndex() == 3);
        const bool two = m_second->isChecked();
        panel.setRowVisible(m_dir2, two);
        panel.setRowVisible(m_dir2Field, two && m_dir2->currentIndex() == 3);
        panel.setRowVisible(m_count2, two);
        panel.setRowVisible(m_spacing2, two);
    }
    if(m_axisBox) panel.setRowVisible(m_axisField, m_axisBox->currentIndex() == 3);
}

void PatternCommand::activate(Field f) {
    m_active = f;
    const std::pair<SelectionField *, Field> fields[] = {{m_objectsField, Field::Objects}, {m_planeField, Field::Plane},
                                                         {m_dir1Field, Field::Dir1},       {m_dir2Field, Field::Dir2},
                                                         {m_axisField, Field::Axis}};
    for(const auto &[sf, fld] : fields)
        if(sf) sf->setActive(fld == f);
    SelectFilter sf;
    sf.vertices = sf.profiles = sf.bodies = false;
    switch(f) {
    case Field::Objects:
        sf.edges = false;
        sf.faceSelectsBody = !featuresMode();
        break;
    case Field::Plane:
        sf.edges = false;
        sf.planes = true;
        sf.planarFacesOnly = true;
        break;
    case Field::Dir1:
    case Field::Dir2:
        sf.faces = false;
        sf.linearEdgesOnly = true;
        break;
    case Field::Axis:
        break; // edges (straight or round) and cylinders
    }
    m_ctx.view->setFilter(sf);
    updateMarks();
}

std::optional<cad::FeatureId> PatternCommand::featureOfFace(const SelectionItem &item) const {
    if(item.kind != SelectionItem::Kind::Face) return std::nullopt;
    const cad::StatePtr st = m_ctx.view->state();
    const cad::Body *b = st ? st->body(item.body) : nullptr;
    if(!b || item.index < 1 || item.index > b->shape.faceCount()) return std::nullopt;
    static const std::regex re("^f(\\d+)/");
    std::smatch m;
    const std::string name = b->shape.faceName(item.index);
    if(!std::regex_search(name, m, re)) return std::nullopt;
    const cad::FeatureId id = std::stoi(m[1]);
    const cad::FeaturePtr f = m_ctx.doc->feature(id);
    if(!f || (f->type() != cad::FeatureType::Hole && f->type() != cad::FeatureType::Extrude)) return std::nullopt;
    const int limit = isEditing() ? m_ctx.doc->indexOf(m_editing) : m_ctx.doc->marker();
    if(m_ctx.doc->indexOf(id) >= limit) return std::nullopt;
    return id;
}

void PatternCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState()) {
        if(!featuresMode())
            for(const auto &b : m_bodies) markInput(*m_ctx.view, *base, b, -1, kObject, marks);
        else
            for(cad::FeatureId id : m_features) {
                const std::string prefix = "f" + std::to_string(id) + "/";
                for(const auto &kv : base->bodies) {
                    const auto mesh = kv.second->meshIfReady();
                    if(!mesh) continue;
                    for(int i = 1; i <= kv.second->shape.faceCount(); ++i)
                        if(kv.second->shape.faceName(i).rfind(prefix, 0) == 0) {
                            QColor c = kObject;
                            c.setAlpha(110);
                            marks.faces.push_back({mesh, i, c});
                        }
                }
            }
        for(const auto *r : {&m_plane, &m_dir1Ref, &m_dir2Ref, &m_axisRef})
            if(*r) markInput(*m_ctx.view, *base, **r, -1, kRef, marks);
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_objectsField->setCount(featuresMode() ? int(m_features.size()) : int(m_bodies.size()));
    if(m_planeField) m_planeField->setCount(m_plane ? 1 : 0);
    if(m_dir1Field) m_dir1Field->setCount(m_dir1Ref ? 1 : 0);
    if(m_dir2Field) m_dir2Field->setCount(m_dir2Ref ? 1 : 0);
    if(m_axisField) m_axisField->setCount(m_axisRef ? 1 : 0);
}

void PatternCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    switch(m_active) {
    case Field::Objects:
        if(featuresMode()) {
            const auto id = featureOfFace(*item);
            if(!id) return;
            auto it = std::find(m_features.begin(), m_features.end(), *id);
            if(it != m_features.end()) m_features.erase(it);
            else m_features.push_back(*id);
        } else {
            if(item->kind != SelectionItem::Kind::Body) return;
            toggleRef(m_bodies, InputRef::ofBody(item->body));
            if(m_planeField && !m_plane && m_bodies.size() == 1) {
                activate(Field::Plane); // the plane next
                emit inputsChanged();
                return;
            }
        }
        break;
    case Field::Plane: {
        const auto p = m_ctx.view->planeRefOf(*item);
        if(!p) return;
        m_plane = InputRef::ofPlane(*p);
        break;
    }
    case Field::Dir1:
    case Field::Dir2:
    case Field::Axis: {
        if(item->kind != SelectionItem::Kind::Edge && item->kind != SelectionItem::Kind::Face) return;
        const auto r = inputRefOf(*m_ctx.view, *item);
        if(!r) return;
        (m_active == Field::Dir1 ? m_dir1Ref : m_active == Field::Dir2 ? m_dir2Ref : m_axisRef) = r;
        break;
    }
    }
    updateMarks();
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> PatternCommand::build(QString &why) {
    const bool features = featuresMode();
    if(features ? m_features.empty() : m_bodies.empty()) {
        why = features ? tr("Pick a face of each hole or extrude to repeat.") : tr("Select the bodies to repeat.");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::PatternFeature>(m_original->clone())
                        : std::make_shared<cad::PatternFeature>();
    f->kind = m_kind;
    f->bodies.clear();
    f->features.clear();
    if(features) f->features = m_features;
    else
        for(const auto &b : m_bodies) f->bodies.push_back(b.body);
    f->join = m_join->isChecked();
    auto axisOf = [&](QComboBox *box, const std::optional<InputRef> &ref, const char *const names[],
                      cad::PatternAxis &out) -> bool {
        out = cad::PatternAxis();
        if(box->currentIndex() < 3) {
            out.builtin = names[box->currentIndex()];
            return true;
        }
        if(!ref) return false;
        out.ref = ref->topo;
        return true;
    };
    auto valid = [&](std::initializer_list<ValueField *> vs) {
        for(ValueField *v : vs)
            if(!v->valid()) {
                why = tr("Enter the count, spacing and angle.");
                return false;
            }
        return true;
    };
    switch(m_kind) {
    case cad::PatternKind::Mirror: {
        const auto ref = m_plane ? m_plane->planeRef() : std::nullopt;
        if(!ref) {
            why = tr("Select the mirror plane.");
            return nullptr;
        }
        f->plane = *ref;
        break;
    }
    case cad::PatternKind::Rectangular:
        if(!axisOf(m_dir1, m_dir1Ref, kDirNames, f->dir1)) {
            why = tr("Select the edge to repeat along.");
            return nullptr;
        }
        if(!valid({m_count1, m_spacing1})) return nullptr;
        f->count1 = {m_count1Slot.name, m_count1->expression().toStdString()};
        f->spacing1 = {m_spacing1Slot.name, m_spacing1->expression().toStdString()};
        f->dir2 = cad::PatternAxis();
        if(m_second->isChecked()) {
            if(!axisOf(m_dir2, m_dir2Ref, kDirNames, f->dir2)) {
                why = tr("Select the second direction's edge.");
                return nullptr;
            }
            if(!valid({m_count2, m_spacing2})) return nullptr;
            f->count2 = {m_count2Slot.name, m_count2->expression().toStdString()};
            f->spacing2 = {m_spacing2Slot.name, m_spacing2->expression().toStdString()};
        }
        break;
    case cad::PatternKind::Circular:
        if(!axisOf(m_axisBox, m_axisRef, kAxisNames, f->axis)) {
            why = tr("Select the axis: an edge, a circle or a cylinder.");
            return nullptr;
        }
        if(!valid({m_count, m_angle})) return nullptr;
        f->count = {m_countSlot.name, m_count->expression().toStdString()};
        f->angle = {m_angleSlot.name, m_angle->expression().toStdString()};
        f->symmetric = m_symmetric->isChecked();
        break;
    }
    return f;
}

} // namespace cadly
