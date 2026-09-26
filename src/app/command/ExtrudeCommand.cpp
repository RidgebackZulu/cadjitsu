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
    m_profilesField = panel.addSelection(tr("Profiles"), tr("Select profiles or faces"), "extrudeProfiles");
    m_direction = panel.addChoice(tr("Direction"), {tr("One Side"), tr("Two Sides"), tr("Symmetric")}, "extrudeDirection");
    m_extent = panel.addChoice(tr("Extent Type"), extents, "extrudeExtent");
    m_objectField = panel.addSelection(tr("Object"), tr("Select a face"), "extrudeObject");
    m_distanceField = panel.addValue(tr("Distance"), cad::ValueKind::Length, evaluator(), "extrudeDistance");
    m_flip = panel.addCheck(tr("Flip"), "extrudeFlip");
    m_taperField = panel.addValue(tr("Taper Angle"), cad::ValueKind::Angle, evaluator(), "extrudeTaper");
    m_taperField->setToolTip(tr("Positive angles flare the sides outwards, negative angles draw them in."));
    m_sideTwo = panel.addSection(tr("Side Two"));
    m_extent2 = panel.addChoice(tr("Extent Type"), extents, "extrudeExtent2");
    m_object2Field = panel.addSelection(tr("Object"), tr("Select a face"), "extrudeObject2");
    m_distance2Field = panel.addValue(tr("Distance"), cad::ValueKind::Length, evaluator(), "extrudeDistance2");
    m_taper2Field = panel.addValue(tr("Taper Angle"), cad::ValueKind::Angle, evaluator(), "extrudeTaper2");
    m_operation = panel.addChoice(tr("Operation"), {tr("Join"), tr("Cut"), tr("Intersect"), tr("New Body")},
                                  "extrudeOperation");

    auto slot = [&](const cad::ParamSlot &s, const char *fallback) {
        return s.empty() ? doc.makeSlot(fallback) : s;
    };
    if(m_original) {
        m_profiles = m_original->profiles;
        m_faces = m_original->faces;
        m_object = m_original->toObject;
        m_object2 = m_original->toObject2;
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
        // Start from what is selected, as in Fusion (select a profile, press E).
        const cad::StatePtr st = m_ctx.view->state();
        m_profiles = m_initial;
        const std::vector<SelectionItem> picked =
            m_initial.empty() ? m_ctx.view->selection().items() : std::vector<SelectionItem>{};
        for(const auto &it : picked) {
            if(it.kind == SelectionItem::Kind::Profile) {
                if(const cad::Profile *p = m_ctx.view->profileOf(it)) m_profiles.push_back({it.feature, it.key, p->sample});
            } else if(it.kind == SelectionItem::Kind::Face && st) {
                const cad::Body *b = st->body(it.body);
                gp_Pln pln;
                if(b && cad::planeOfFace(b->shape.face(it.index), pln))
                    m_faces.push_back(cad::makeTopoRef(*b, cad::TopoKind::Face, it.index));
            }
        }
    }
    m_distanceField->setExpression(QString::fromStdString(m_distance.expr));
    m_taperField->setExpression(QString::fromStdString(m_taper.expr));
    m_distance2Field->setExpression(QString::fromStdString(m_distance2.expr));
    m_taper2Field->setExpression(QString::fromStdString(m_taper2.expr));

    connect(m_profilesField, &SelectionField::activated, this, [this] { activate(Field::Profiles); });
    connect(m_objectField, &SelectionField::activated, this, [this] { activate(Field::Object); });
    connect(m_object2Field, &SelectionField::activated, this, [this] { activate(Field::Object2); });
    connect(m_profilesField, &SelectionField::cleared, this, [this] {
        m_profiles.clear();
        m_faces.clear();
        activate(Field::Profiles);
        changed();
    });
    connect(m_objectField, &SelectionField::cleared, this, [this] {
        m_object = cad::TopoRef();
        activate(Field::Object);
        changed();
    });
    connect(m_object2Field, &SelectionField::cleared, this, [this] {
        m_object2 = cad::TopoRef();
        activate(Field::Object2);
        changed();
    });
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

    activate(Field::Profiles);
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
    panel.setRowVisible(m_objectField, e1 == cad::ExtentType::ToObject);
    panel.setRowVisible(m_distanceField, e1 == cad::ExtentType::Distance);
    panel.setRowVisible(m_flip, e1 == cad::ExtentType::ThroughAll &&
                                    kDirections[m_direction->currentIndex()] == cad::ExtrudeDirection::OneSide);
    m_sideTwo->setVisible(two);
    panel.setRowVisible(m_extent2, two);
    panel.setRowVisible(m_object2Field, two && e2 == cad::ExtentType::ToObject);
    panel.setRowVisible(m_distance2Field, two && e2 == cad::ExtentType::Distance);
    panel.setRowVisible(m_taper2Field, two);
    if(m_active == Field::Object && e1 != cad::ExtentType::ToObject) activate(Field::Profiles);
    if(m_active == Field::Object2 && !(two && e2 == cad::ExtentType::ToObject)) activate(Field::Profiles);
    // Picking a target face comes next when To Object is chosen.
    if(e1 == cad::ExtentType::ToObject && m_object.empty() && m_active == Field::Profiles && profileCount() > 0)
        activate(Field::Object);
}

void ExtrudeCommand::activate(Field f) {
    m_active = f;
    m_profilesField->setActive(f == Field::Profiles);
    m_objectField->setActive(f == Field::Object);
    m_object2Field->setActive(f == Field::Object2);
    // Profiles take sketch regions and planar faces; objects are faces.
    m_ctx.view->setSelectable(true, false, false, false, f == Field::Profiles);
    showSelection();
}

// Shows the active field's inputs as the canvas selection.
void ExtrudeCommand::showSelection() {
    const cad::StatePtr st = m_ctx.view->state();
    SelectionSet sel;
    auto addFace = [&](const cad::TopoRef &ref) {
        if(ref.empty() || !st) return;
        const cad::ResolvedRef r = cad::resolveRef(*st, ref);
        if(!r.ok) return;
        SelectionItem it;
        it.kind = SelectionItem::Kind::Face;
        it.body = r.body->id;
        it.index = r.index;
        sel.add(it);
    };
    if(m_active == Field::Profiles) {
        for(const auto &p : m_profiles) {
            SelectionItem it;
            it.kind = SelectionItem::Kind::Profile;
            it.feature = p.sketch;
            it.key = p.key;
            sel.add(it);
        }
        for(const auto &f : m_faces) addFace(f);
    } else {
        addFace(m_active == Field::Object ? m_object : m_object2);
    }
    m_syncing = true;
    m_ctx.view->setSelection(sel);
    m_syncing = false;
    m_profilesField->setCount(profileCount());
    m_objectField->setCount(m_object.empty() ? 0 : 1);
    m_object2Field->setCount(m_object2.empty() ? 0 : 1);
}

void ExtrudeCommand::selectionChanged() {
    if(m_syncing) return;
    const cad::StatePtr st = m_ctx.view->state();
    if(!st) return;
    // Faces the preview itself made are not inputs.
    const cad::FeatureId self = isEditing() ? m_editing : m_ctx.doc->nextFeatureId();
    const std::string ownPrefix = "f" + std::to_string(self) + "/";
    auto faceRef = [&](const SelectionItem &it, cad::TopoRef &out) {
        const cad::Body *b = st->body(it.body);
        if(!b || it.index < 1 || it.index > b->shape.faceCount()) return false;
        gp_Pln pln;
        if(!cad::planeOfFace(b->shape.face(it.index), pln)) return false;
        out = cad::makeTopoRef(*b, cad::TopoKind::Face, it.index);
        return out.name.rfind(ownPrefix, 0) != 0;
    };
    const SelectionSet &sel = m_ctx.view->selection();
    if(m_active == Field::Profiles) {
        std::vector<cad::ProfileRef> profiles;
        std::vector<cad::TopoRef> faces;
        for(const auto &it : sel.items()) {
            if(it.kind == SelectionItem::Kind::Profile) {
                if(const cad::Profile *p = m_ctx.view->profileOf(it)) profiles.push_back({it.feature, it.key, p->sample});
            } else if(it.kind == SelectionItem::Kind::Face) {
                cad::TopoRef r;
                if(faceRef(it, r)) faces.push_back(r);
            }
        }
        m_profiles = std::move(profiles);
        m_faces = std::move(faces);
    } else {
        cad::TopoRef picked;
        for(const auto &it : sel.items()) {
            cad::TopoRef r;
            if(it.kind == SelectionItem::Kind::Face && faceRef(it, r)) picked = r;
        }
        (m_active == Field::Object ? m_object : m_object2) = picked;
    }
    showSelection();
    changed();
}

std::set<cad::FeatureId> ExtrudeCommand::sketchesToShow() const {
    std::set<cad::FeatureId> out;
    for(const auto &p : m_profiles) out.insert(p.sketch);
    return out;
}

void ExtrudeCommand::changed() {
    updateArrow();
    chooseOperation();
    emit inputsChanged();
}

void ExtrudeCommand::previewed(const cad::StatePtr &) {
    // Face inputs are re-found in the model now on screen; the base model may
    // only now be known (it is computed in the background).
    showSelection();
    updateArrow();
    if(chooseOperation()) emit inputsChanged();
}

bool ExtrudeCommand::inputPoint(gp_Pnt &p, gp_Dir &n) const {
    const cad::StatePtr st = baseState();
    if(!st) return false;
    for(const auto &pr : m_profiles) {
        const cad::SketchResult *sk = nullptr;
        cad::Status s;
        const cad::Profile *prof = cad::resolveProfile(*st, pr, sk, s);
        if(!prof || !sk) continue;
        p = sk->toWorld(prof->sample);
        n = sk->frame.Direction();
        return true;
    }
    for(const auto &fr : m_faces) {
        const cad::ResolvedRef r = cad::resolveRef(*st, fr);
        if(!r.ok) continue;
        const TopoDS_Face face = TopoDS::Face(r.shape);
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
        m_arrow.label = m_distanceField->expression();
    }
    m_ctx.viewport->refreshOverlay();
}

// Join when extruding out of a body's face, Cut when extruding into a body,
// otherwise a new body (like Fusion's automatic operation).
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
    if(m_profiles.empty() && m_faces.empty()) {
        why = tr("Select profiles or planar faces to extrude.");
        return nullptr;
    }
    const cad::ExtrudeDirection dir = kDirections[m_direction->currentIndex()];
    const cad::ExtentType e1 = kExtents[m_extent->currentIndex()], e2 = kExtents[m_extent2->currentIndex()];
    const bool two = dir == cad::ExtrudeDirection::TwoSides;
    if(e1 == cad::ExtentType::ToObject && m_object.empty()) {
        why = tr("Select the face to extrude to.");
        return nullptr;
    }
    if(two && e2 == cad::ExtentType::ToObject && m_object2.empty()) {
        why = tr("Select the face to extrude side two to.");
        return nullptr;
    }
    auto bad = [&](ValueField *f, const QString &what) {
        if(f->valid()) return false;
        why = tr("%1: %2").arg(what, f->toolTip().isEmpty() ? tr("enter a value") : f->toolTip());
        return true;
    };
    if(e1 == cad::ExtentType::Distance && bad(m_distanceField, tr("Distance"))) return nullptr;
    if(bad(m_taperField, tr("Taper angle"))) return nullptr;
    if(two && e2 == cad::ExtentType::Distance && bad(m_distance2Field, tr("Distance (side two)"))) return nullptr;
    if(two && bad(m_taper2Field, tr("Taper angle (side two)"))) return nullptr;

    auto f = m_original ? std::static_pointer_cast<cad::ExtrudeFeature>(m_original->clone())
                        : std::make_shared<cad::ExtrudeFeature>();
    f->profiles = m_profiles;
    f->faces = m_faces;
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
    f->toObject = e1 == cad::ExtentType::ToObject ? m_object : cad::TopoRef();
    f->toObject2 = two && e2 == cad::ExtentType::ToObject ? m_object2 : cad::TopoRef();
    f->flip = e1 == cad::ExtentType::ThroughAll && dir == cad::ExtrudeDirection::OneSide && m_flip->isChecked();
    f->operation = operation();
    f->participants.clear();
    return f;
}

} // namespace cadly
