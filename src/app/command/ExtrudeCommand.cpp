#include "command/ExtrudeCommand.h"

#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <QCheckBox>
#include <QSettings>
#include <QComboBox>
#include <QLabel>

#include <cmath>

namespace cadly {

namespace {

// Combo box orders, as in Fusion's dialog.
const cad::ExtrudeDirection kDirections[] = {cad::ExtrudeDirection::OneSide, cad::ExtrudeDirection::TwoSides,
                                             cad::ExtrudeDirection::Symmetric};
const cad::ExtentType kExtents[] = {cad::ExtentType::Distance, cad::ExtentType::ToObject, cad::ExtentType::ThroughAll};
const cad::BodyOperation kOperations[] = {cad::BodyOperation::Join, cad::BodyOperation::Cut,
                                          cad::BodyOperation::Intersect, cad::BodyOperation::NewBody};

const QColor kActiveInput(20, 100, 225);
const QColor kOtherInput(120, 150, 200);

template <class T, size_t N> int indexIn(const T (&list)[N], T v) {
    for(size_t i = 0; i < N; ++i)
        if(list[i] == v) return int(i);
    return 0;
}

QVector3D toQ(const gp_XYZ &v) { return QVector3D(float(v.X()), float(v.Y()), float(v.Z())); }

} // namespace

ExtrudeCommand::ExtrudeCommand(const CommandContext &ctx, cad::FeatureId editing)
    : Command(ctx, editing), m_arrow(ctx.viewport) {}

IconId ExtrudeCommand::iconId() const { return IconId::Extrude; }

cad::BodyOperation ExtrudeCommand::operation() const {
    return kOperations[std::clamp(m_operation->currentIndex(), 0, 3)];
}

void ExtrudeCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::ExtrudeFeature>(doc.feature(m_editing));
    const QStringList extents = {tr("Distance"), tr("To Object"), tr("All")};
    m_fields[Profiles] = panel.addSelection(tr("Profiles"), tr("Select profiles or faces"), "extrudeProfiles");
    m_direction = panel.addChoice(tr("Direction"), {tr("One Side"), tr("Two Sides"), tr("Symmetric")}, "extrudeDirection");
    m_extent = panel.addChoice(tr("Extent Type"), extents, "extrudeExtent");
    m_fields[Object] = panel.addSelection(tr("Object"), tr("Select a face"), "extrudeObject");
    m_distanceField = panel.addValue(tr("Distance"), cad::ValueKind::Length, evaluator(), "extrudeDistance");
    m_flip = panel.addCheck(tr("Flip"), "extrudeFlip");
    m_taperField = panel.addValue(tr("Taper Angle"), cad::ValueKind::Angle, evaluator(), "extrudeTaper");
    m_taperField->setToolTip(tr("Positive angles flare the sides outwards, negative angles draw them in."));
    m_sideTwo = panel.addSection(tr("Side Two"));
    m_extent2 = panel.addChoice(tr("Extent Type"), extents, "extrudeExtent2");
    m_fields[Object2] = panel.addSelection(tr("Object"), tr("Select a face"), "extrudeObject2");
    m_distance2Field = panel.addValue(tr("Distance"), cad::ValueKind::Length, evaluator(), "extrudeDistance2");
    m_taper2Field = panel.addValue(tr("Taper Angle"), cad::ValueKind::Angle, evaluator(), "extrudeTaper2");
    m_operation = panel.addChoice(tr("Operation"), {tr("Join"), tr("Cut"), tr("Intersect"), tr("New Body")},
                                  "extrudeOperation");

    auto slot = [&](const cad::ParamSlot &s, const char *fallback) {
        return s.empty() ? doc.makeSlot(fallback) : s;
    };
    if(m_original) {
        for(const auto &p : m_original->profiles) m_refs[Profiles].push_back(InputRef::ofProfile(p));
        for(const auto &f : m_original->faces) m_refs[Profiles].push_back(InputRef::ofTopo(f));
        if(!m_original->toObject.empty()) m_refs[Object].push_back(InputRef::ofTopo(m_original->toObject));
        if(!m_original->toObject2.empty()) m_refs[Object2].push_back(InputRef::ofTopo(m_original->toObject2));
        m_distance = slot(m_original->distance, "10 mm");
        m_taper = slot(m_original->taper, "0 deg");
        m_distance2 = slot(m_original->distance2, "10 mm");
        m_taper2 = slot(m_original->taper2, "0 deg");
        m_direction->setCurrentIndex(indexIn(kDirections, m_original->direction));
        m_extent->setCurrentIndex(indexIn(kExtents, m_original->extent));
        m_extent2->setCurrentIndex(indexIn(kExtents, m_original->extent2));
        m_flip->setChecked(m_original->flip);
        m_operation->setCurrentIndex(indexIn(kOperations, m_original->operation));
        m_operationChosen = true;
    } else {
        m_distance = doc.makeSlot("10 mm");
        m_taper = doc.makeSlot("0 deg");
        m_distance2 = doc.makeSlot("10 mm");
        m_taper2 = doc.makeSlot("0 deg");
        m_operation->setCurrentIndex(indexIn(kOperations, cad::BodyOperation::NewBody));
        // A new body unless automatic Join / Cut is on (combine bodies afterwards).
        m_operationChosen = !autoOperation();
        // Start from what is selected, as in Fusion (select a profile, press E).
        for(const auto &p : m_initial) m_refs[Profiles].push_back(InputRef::ofProfile(p));
        if(m_initial.empty())
            for(const auto &it : m_ctx.view->selection().items()) {
                const auto r = inputRefOf(*m_ctx.view, it);
                const bool planarFace = it.kind == SelectionItem::Kind::Face && m_ctx.view->planeRefOf(it);
                if(r && (r->kind == SelectionItem::Kind::Profile || planarFace)) m_refs[Profiles].push_back(*r);
            }
    }
    // A new extrude waits for its distance (typed, or dragged with the arrow).
    m_distanceField->setExpression(m_original ? QString::fromStdString(m_distance.expr) : QString());
    m_taperField->setExpression(QString::fromStdString(m_taper.expr));
    m_distance2Field->setExpression(QString::fromStdString(m_distance2.expr));
    m_taper2Field->setExpression(QString::fromStdString(m_taper2.expr));

    for(Field f : {Profiles, Object, Object2}) {
        connect(m_fields[f], &SelectionField::activated, this, [this, f] { activate(f); });
        connect(m_fields[f], &SelectionField::cleared, this, [this, f] {
            m_refs[f].clear();
            activate(f);
            changed();
        });
    }
    m_lastExtent = kExtents[m_extent->currentIndex()];
    connect(m_extent, &QComboBox::currentIndexChanged, this, [this](int i) {
        // All carries on the way the arrow pointed.
        if(kExtents[i] == cad::ExtentType::ThroughAll && m_lastExtent == cad::ExtentType::Distance) {
            const QSignalBlocker block(m_flip);
            m_flip->setChecked(m_distanceField->value().value_or(0.0) < 0.0);
        }
        m_lastExtent = kExtents[i];
        updateRows();
        changed();
    });
    for(QComboBox *c : {m_direction, m_extent2})
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            updateRows();
            changed();
        });
    connect(m_operation, &QComboBox::activated, this, [this] {
        m_operationChosen = true;
        changed();
    });
    connect(m_flip, &QCheckBox::toggled, this, [this] { changed(); });
    for(ValueField *v : {m_distanceField, m_taperField, m_distance2Field, m_taper2Field})
        connect(v, &ValueField::edited, this, [this] { changed(); });
    m_arrow.onDrag = [this](double d) {
        m_distanceField->setExpression(QString::fromStdString(SketchEditor::formatExpression(d, cad::ValueKind::Length)));
        changed();
    };

    activate(Profiles);
    updateRows();
    updateArrow();
    chooseOperation();
    // Type the distance straight away, as in Fusion.
    if(profileCount() > 0 && kExtents[m_extent->currentIndex()] == cad::ExtentType::Distance) {
        m_distanceField->setFocus(Qt::OtherFocusReason);
        m_distanceField->selectAll();
    }
}

void ExtrudeCommand::updateRows() {
    CommandPanel &panel = *m_ctx.panel;
    const cad::ExtentType e1 = kExtents[m_extent->currentIndex()], e2 = kExtents[m_extent2->currentIndex()];
    const bool two = kDirections[m_direction->currentIndex()] == cad::ExtrudeDirection::TwoSides;
    panel.setRowVisible(m_fields[Object], e1 == cad::ExtentType::ToObject);
    panel.setRowVisible(m_distanceField, e1 == cad::ExtentType::Distance);
    panel.setRowVisible(m_flip, e1 == cad::ExtentType::ThroughAll &&
                                    kDirections[m_direction->currentIndex()] == cad::ExtrudeDirection::OneSide);
    m_sideTwo->setVisible(two);
    panel.setRowVisible(m_extent2, two);
    panel.setRowVisible(m_fields[Object2], two && e2 == cad::ExtentType::ToObject);
    panel.setRowVisible(m_distance2Field, two && e2 == cad::ExtentType::Distance);
    panel.setRowVisible(m_taper2Field, two);
    if(m_active == Object && e1 != cad::ExtentType::ToObject) activate(Profiles);
    if(m_active == Object2 && !(two && e2 == cad::ExtentType::ToObject)) activate(Profiles);
    // Picking a target face comes next when To Object is chosen.
    if(e1 == cad::ExtentType::ToObject && m_refs[Object].empty() && m_active == Profiles && profileCount() > 0)
        activate(Object);
}

void ExtrudeCommand::activate(Field f) {
    m_active = f;
    for(Field g : {Profiles, Object, Object2}) m_fields[g]->setActive(g == f);
    // Profiles take sketch regions and planar faces; objects are one planar face.
    SelectFilter sf;
    sf.edges = sf.vertices = sf.bodies = false;
    sf.planarFacesOnly = true;
    sf.profiles = f == Profiles;
    m_ctx.view->setFilter(sf);
    updateMarks();
}

// Draws the inputs on the model the extrude starts from.
void ExtrudeCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState())
        for(Field f : {Profiles, Object, Object2})
            for(size_t i = 0; i < m_refs[f].size(); ++i)
                markInput(*m_ctx.view, *base, m_refs[f][i], int(f) * 1000 + int(i),
                          f == m_active ? kActiveInput : kOtherInput, marks);
    m_ctx.view->setInputMarks(std::move(marks));
    for(Field f : {Profiles, Object, Object2}) m_fields[f]->setCount(int(m_refs[f].size()));
}

void ExtrudeCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
    // The preview's own faces are not inputs.
    if(!r || r->createdBy(isEditing() ? m_editing : m_ctx.doc->nextFeatureId())) return;
    const bool face = item->kind == SelectionItem::Kind::Face && m_ctx.view->planeRefOf(*item).has_value();
    if(m_active == Profiles) {
        // Clicks add and remove, as in Fusion's command inputs.
        if(r->kind != SelectionItem::Kind::Profile && !face) return;
        toggleRef(m_refs[Profiles], *r);
    } else {
        if(!face) return;
        m_refs[m_active] = {*r};
    }
    updateMarks();
    changed();
}

void ExtrudeCommand::markClicked(int tag) {
    const size_t f = size_t(tag / 1000), i = size_t(tag % 1000);
    if(f > 2 || i >= m_refs[f].size()) return;
    m_refs[f].erase(m_refs[f].begin() + std::ptrdiff_t(i));
    updateMarks();
    changed();
}

std::set<cad::FeatureId> ExtrudeCommand::sketchesToShow() const {
    std::set<cad::FeatureId> out;
    for(const auto &r : m_refs[Profiles])
        if(r.kind == SelectionItem::Kind::Profile) out.insert(r.profile.sketch);
    return out;
}

void ExtrudeCommand::changed() {
    updateArrow();
    chooseOperation();
    emit inputsChanged();
}

void ExtrudeCommand::previewed(const cad::StatePtr &) {
    // The base model may only now be known (it is computed in the background).
    updateMarks();
    updateArrow();
    if(chooseOperation()) emit inputsChanged();
}

bool ExtrudeCommand::inputPoint(gp_Pnt &p, gp_Dir &n) const {
    const cad::StatePtr st = baseState();
    if(!st) return false;
    for(const auto &r : m_refs[Profiles]) {
        if(r.kind == SelectionItem::Kind::Profile) {
            const cad::SketchResult *sk = nullptr;
            cad::Status s;
            const cad::Profile *prof = cad::resolveProfile(*st, r.profile, sk, s);
            if(!prof || !sk) continue;
            p = sk->toWorld(prof->sample);
            n = sk->frame.Direction();
            return true;
        }
        const cad::ResolvedRef res = cad::resolveRef(*st, r.topo);
        if(!res.ok) continue;
        const TopoDS_Face face = TopoDS::Face(res.shape);
        gp_Pln pln;
        if(!cad::planeOfFace(face, pln)) continue;
        GProp_GProps g;
        BRepGProp::SurfaceProperties(face, g);
        p = g.CentreOfMass();
        n = pln.Axis().Direction();
        return true;
    }
    return false;
}

void ExtrudeCommand::updateArrow() {
    gp_Pnt p;
    gp_Dir n;
    const bool show = kExtents[m_extent->currentIndex()] == cad::ExtentType::Distance && inputPoint(p, n);
    m_arrow.setVisible(show);
    if(show) {
        m_arrow.setAxis(toQ(p.XYZ()), toQ(n.XYZ()));
        m_arrow.setDistance(m_distanceField->value().value_or(0.0));
        // The value shows in the on-canvas box instead of a label.
    }
    m_ctx.viewport->refreshOverlay();
}

// Join when extruding out of a body's face, Cut when extruding into a body,
// otherwise a new body (like Fusion's automatic operation).
bool ExtrudeCommand::autoOperation() {
    return QSettings().value(QStringLiteral("modeling/autoOperation"), false).toBool();
}

void ExtrudeCommand::setAutoOperation(bool on) { QSettings().setValue(QStringLiteral("modeling/autoOperation"), on); }

bool ExtrudeCommand::chooseOperation() {
    if(m_operationChosen) return false;
    gp_Pnt p;
    gp_Dir n;
    const cad::StatePtr st = baseState();
    if(!st || !inputPoint(p, n)) return false;
    if(st != m_probed) {
        m_probed = st;
        m_inside.clear();
    }
    auto inside = [&](const gp_Pnt &q) {
        const std::array<double, 3> key{q.X(), q.Y(), q.Z()};
        if(auto it = m_inside.find(key); it != m_inside.end()) return it->second;
        bool in = false;
        for(const auto &kv : st->bodies) {
            // Tessellated first: meshing writes into the shape, and it may be
            // happening in the background.
            kv.second->mesh();
            BRepClass3d_SolidClassifier c(kv.second->shape.shape(), q, 1e-7);
            if(c.State() == TopAbs_IN) {
                in = true;
                break;
            }
        }
        return m_inside[key] = in;
    };
    double sign = 1.0;
    const cad::ExtentType e1 = kExtents[m_extent->currentIndex()];
    if(e1 == cad::ExtentType::Distance && m_distanceField->value() && *m_distanceField->value() < 0) sign = -1.0;
    if(e1 == cad::ExtentType::ThroughAll && m_flip->isChecked()) sign = -1.0;
    const double eps = std::max(1e-3, 1e-4 * st->modelSize());
    const gp_Vec ahead = gp_Vec(n) * (sign * eps);
    cad::BodyOperation op = cad::BodyOperation::NewBody;
    if(kDirections[m_direction->currentIndex()] == cad::ExtrudeDirection::OneSide) {
        if(inside(p.Translated(ahead))) op = cad::BodyOperation::Cut;
        else if(inside(p.Translated(-ahead))) op = cad::BodyOperation::Join;
    } else if(inside(p.Translated(ahead)) || inside(p.Translated(-ahead))) {
        op = cad::BodyOperation::Join;
    }
    if(op == operation()) return false;
    const QSignalBlocker block(m_operation);
    m_operation->setCurrentIndex(indexIn(kOperations, op));
    return true;
}

std::shared_ptr<cad::Feature> ExtrudeCommand::build(QString &why) {
    if(m_refs[Profiles].empty()) {
        why = tr("Select profiles or planar faces to extrude.");
        return nullptr;
    }
    const cad::ExtrudeDirection dir = kDirections[m_direction->currentIndex()];
    const cad::ExtentType e1 = kExtents[m_extent->currentIndex()], e2 = kExtents[m_extent2->currentIndex()];
    const bool two = dir == cad::ExtrudeDirection::TwoSides;
    if(e1 == cad::ExtentType::ToObject && m_refs[Object].empty()) {
        why = tr("Select the face to extrude to.");
        return nullptr;
    }
    if(two && e2 == cad::ExtentType::ToObject && m_refs[Object2].empty()) {
        why = tr("Select the face to extrude side two to.");
        return nullptr;
    }
    auto bad = [&](ValueField *f, const QString &what) {
        if(f->valid()) return false;
        if(f->expression().isEmpty()) {
            why = f == m_distanceField ? tr("Type the distance, or drag the arrow.") : tr("%1: enter a value").arg(what);
            return true;
        }
        why = tr("%1: %2").arg(what, f->toolTip().isEmpty() ? tr("enter a value") : f->toolTip());
        return true;
    };
    if(e1 == cad::ExtentType::Distance && bad(m_distanceField, tr("Distance"))) return nullptr;
    if(bad(m_taperField, tr("Taper angle"))) return nullptr;
    if(two && e2 == cad::ExtentType::Distance && bad(m_distance2Field, tr("Distance (side two)"))) return nullptr;
    if(two && bad(m_taper2Field, tr("Taper angle (side two)"))) return nullptr;

    auto f = m_original ? std::static_pointer_cast<cad::ExtrudeFeature>(m_original->clone())
                        : std::make_shared<cad::ExtrudeFeature>();
    f->profiles.clear();
    f->faces.clear();
    for(const auto &r : m_refs[Profiles]) {
        if(r.kind == SelectionItem::Kind::Profile) {
            cad::ProfileRef p = r.profile;
            // Remember the region's shape, so curves added to the sketch later
            // do not change this extrude.
            if(const cad::StatePtr base = baseState()) cad::captureProfileOutline(*base, p);
            f->profiles.push_back(p);
        }
        else f->faces.push_back(r.topo);
    }
    f->direction = dir;
    f->extent = e1;
    f->extent2 = e2;
    f->distance = {m_distance.name, m_distanceField->expression().toStdString()};
    f->taper = {m_taper.name, m_taperField->expression().toStdString()};
    if(two) {
        f->distance2 = {m_distance2.name, m_distance2Field->expression().toStdString()};
        f->taper2 = {m_taper2.name, m_taper2Field->expression().toStdString()};
    } else {
        f->distance2 = {};
        f->taper2 = {};
    }
    f->toObject = e1 == cad::ExtentType::ToObject ? m_refs[Object].front().topo : cad::TopoRef();
    f->toObject2 = two && e2 == cad::ExtentType::ToObject ? m_refs[Object2].front().topo : cad::TopoRef();
    f->flip = e1 == cad::ExtentType::ThroughAll && dir == cad::ExtrudeDirection::OneSide && m_flip->isChecked();
    f->operation = operation();
    f->participants.clear();
    return f;
}

} // namespace cadly
