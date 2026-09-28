#include "command/SplitCommand.h"

#include "model/ModelView.h"
#include "ui/Icons.h"
#include "viewport/RenderScene.h"

#include <QCheckBox>
#include <QComboBox>

#include <algorithm>

namespace cadjitsu {

namespace {
const QColor kActive(20, 100, 225);
const QColor kOther(120, 150, 200);
const QColor kBody(230, 140, 40);
const cad::SplitKeep kKeeps[] = {cad::SplitKeep::Both, cad::SplitKeep::Front, cad::SplitKeep::Back};
} // namespace

void CurveMarks::contribute(RenderScene &scene) {
    LineBatch b;
    b.color = QColor(20, 100, 225);
    b.width = 3.2f;
    b.depthTest = false;
    for(const auto &poly : polylines)
        for(size_t i = 0; i + 1 < poly.size(); ++i) b.segments.insert(b.segments.end(), {poly[i], poly[i + 1]});
    if(!b.segments.empty()) scene.lines.push_back(b);
}

IconId SplitCommand::iconId() const { return IconId::Split; }

void SplitCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::SplitFeature>(doc.feature(m_editing));
    m_toolBox = panel.addChoice(tr("Split With"), {tr("Plane or face"), tr("Sketch curves")}, "splitTool");
    m_planeField = panel.addSelection(tr("Plane"), tr("Select a plane or face"), "splitPlane");
    m_curvesField = panel.addSelection(tr("Curves"), tr("Select sketch curves"), "splitCurves");
    m_bodiesField = panel.addSelection(tr("Bodies"), tr("All it crosses"), "splitBodies");
    m_keep = panel.addChoice(tr("Keep"), {tr("Both sides"), tr("Front side"), tr("Back side")}, "splitKeep");
    m_pins = panel.addCheck(tr("Alignment Pins"), "splitPins");
    m_pinDiameterField = panel.addValue(tr("Pin Hole Ø"), cad::ValueKind::Length, evaluator(), "splitPinDiameter");
    m_pinDepthField = panel.addValue(tr("Pin Depth"), cad::ValueKind::Length, evaluator(), "splitPinDepth");
    m_pins->setToolTip(tr("Drills matching holes into both halves of a plane cut, for pins (a 3 mm dowel, or a "
                          "piece of filament) that line the printed pieces up when you glue them."));

    const cad::SplitFeature *o = m_original.get();
    m_pinDiameter = o && !o->pinDiameter.empty() ? o->pinDiameter : doc.makeSlot("3.2 mm");
    m_pinDepth = o && !o->pinDepth.empty() ? o->pinDepth : doc.makeSlot("6 mm");
    m_pinDiameterField->setExpression(QString::fromStdString(m_pinDiameter.expr));
    m_pinDepthField->setExpression(QString::fromStdString(m_pinDepth.expr));
    if(o) {
        m_toolBox->setCurrentIndex(o->tool == cad::SplitTool::Sketch ? 1 : 0);
        if(o->tool == cad::SplitTool::Plane) m_plane = InputRef::ofPlane(o->plane);
        m_sketch = o->sketch;
        m_curves = o->curves;
        for(const auto &b : o->bodies) m_bodies.push_back(InputRef::ofBody(b));
        for(int i = 0; i < 3; ++i)
            if(kKeeps[i] == o->keep) m_keep->setCurrentIndex(i);
        m_pins->setChecked(o->pins);
    } else {
        for(const auto &it : m_ctx.view->selection().items()) {
            if(it.kind == SelectionItem::Kind::Body) m_bodies.push_back(InputRef::ofBody(it.body));
            else if(!m_plane)
                if(const auto p = m_ctx.view->planeRefOf(it)) m_plane = InputRef::ofPlane(*p);
        }
    }
    connect(m_toolBox, &QComboBox::currentIndexChanged, this, [this] {
        updateRows();
        activate(Field::Tool);
        emit inputsChanged();
    });
    connect(m_keep, &QComboBox::currentIndexChanged, this, [this] {
        updateRows();
        emit inputsChanged();
    });
    connect(m_pins, &QCheckBox::toggled, this, [this] {
        updateRows();
        emit inputsChanged();
    });
    for(ValueField *v : {m_pinDiameterField, m_pinDepthField}) connect(v, &ValueField::edited, this, &Command::inputsChanged);
    connect(m_planeField, &SelectionField::activated, this, [this] { activate(Field::Tool); });
    connect(m_curvesField, &SelectionField::activated, this, [this] { activate(Field::Tool); });
    connect(m_bodiesField, &SelectionField::activated, this, [this] { activate(Field::Bodies); });
    connect(m_planeField, &SelectionField::cleared, this, [this] {
        m_plane.reset();
        activate(Field::Tool);
        emit inputsChanged();
    });
    connect(m_curvesField, &SelectionField::cleared, this, [this] {
        m_curves.clear();
        m_sketch = cad::kNoFeature;
        activate(Field::Tool);
        emit inputsChanged();
    });
    connect(m_bodiesField, &SelectionField::cleared, this, [this] {
        m_bodies.clear();
        activate(Field::Bodies);
        emit inputsChanged();
    });
    m_ctx.view->setOriginForced(true);
    updateRows();
    activate(Field::Tool);
}

bool SplitCommand::sketchMode() const { return m_toolBox->currentIndex() == 1; }

std::set<cad::FeatureId> SplitCommand::sketchesToShow() const {
    if(m_sketch != cad::kNoFeature) return {m_sketch};
    return {};
}

void SplitCommand::updateRows() {
    CommandPanel &panel = *m_ctx.panel;
    const bool sketch = sketchMode();
    panel.setRowVisible(m_planeField, !sketch);
    panel.setRowVisible(m_curvesField, sketch);
    panel.setRowVisible(m_keep, !sketch);
    const bool pinsPossible = !sketch && m_keep->currentIndex() == 0;
    panel.setRowVisible(m_pins, pinsPossible);
    panel.setRowVisible(m_pinDiameterField, pinsPossible && m_pins->isChecked());
    panel.setRowVisible(m_pinDepthField, pinsPossible && m_pins->isChecked());
}

void SplitCommand::activate(Field f) {
    m_active = f;
    const bool sketch = sketchMode();
    m_planeField->setActive(f == Field::Tool && !sketch);
    m_curvesField->setActive(f == Field::Tool && sketch);
    m_bodiesField->setActive(f == Field::Bodies);
    SelectFilter sf;
    sf.vertices = sf.profiles = sf.bodies = false;
    if(f == Field::Bodies) {
        sf.faceSelectsBody = true;
        sf.edges = false;
    } else if(sketch) {
        sf.faces = sf.edges = false;
        sf.sketchCurves = true;
    } else {
        sf.edges = false;
        sf.planes = true;
        sf.planarFacesOnly = true;
    }
    m_ctx.view->setFilter(sf);
    updateMarks();
}

void SplitCommand::updateMarks() {
    ModelView::InputMarks marks;
    if(const cad::StatePtr base = baseState()) {
        if(m_plane && !sketchMode())
            markInput(*m_ctx.view, *base, *m_plane, -1, m_active == Field::Tool ? kActive : kOther, marks);
        for(size_t i = 0; i < m_bodies.size(); ++i) markInput(*m_ctx.view, *base, m_bodies[i], int(i), kBody, marks);
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_curveMarks.polylines.clear();
    if(sketchMode())
        for(int c : m_curves) m_curveMarks.polylines.push_back(m_ctx.view->sketchCurvePolyline(m_sketch, c));
    m_planeField->setCount(m_plane ? 1 : 0);
    m_curvesField->setCount(int(m_curves.size()));
    m_bodiesField->setCount(int(m_bodies.size()));
}

void SplitCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    if(m_active == Field::Bodies) {
        if(item->kind != SelectionItem::Kind::Body) return;
        toggleRef(m_bodies, InputRef::ofBody(item->body));
    } else if(sketchMode()) {
        if(item->kind != SelectionItem::Kind::SketchEntity) return;
        if(m_sketch != item->feature) { // curves come from one sketch
            m_sketch = item->feature;
            m_curves.clear();
        }
        auto it = std::find(m_curves.begin(), m_curves.end(), item->entity);
        if(it != m_curves.end()) m_curves.erase(it);
        else m_curves.push_back(item->entity);
        if(m_curves.empty()) m_sketch = cad::kNoFeature;
    } else {
        const std::optional<cad::PlaneRef> p = m_ctx.view->planeRefOf(*item);
        if(!p) return;
        m_plane = InputRef::ofPlane(*p);
    }
    updateMarks();
    emit inputsChanged();
}

void SplitCommand::markClicked(int tag) {
    if(tag < 0 || tag >= int(m_bodies.size())) return;
    m_bodies.erase(m_bodies.begin() + tag);
    updateMarks();
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> SplitCommand::build(QString &why) {
    const bool sketch = sketchMode();
    const std::optional<cad::PlaneRef> ref = m_plane ? m_plane->planeRef() : std::nullopt;
    if(!sketch && !ref) {
        why = tr("Select the plane or planar face to split with.");
        return nullptr;
    }
    if(sketch && m_curves.empty()) {
        why = tr("Select the sketch curves to split with.");
        return nullptr;
    }
    const bool pins = !sketch && m_keep->currentIndex() == 0 && m_pins->isChecked();
    if(pins && (!m_pinDiameterField->valid() || !m_pinDepthField->valid())) {
        why = tr("Pins: enter the hole diameter and depth");
        return nullptr;
    }
    auto f = m_original ? std::static_pointer_cast<cad::SplitFeature>(m_original->clone())
                        : std::make_shared<cad::SplitFeature>();
    f->tool = sketch ? cad::SplitTool::Sketch : cad::SplitTool::Plane;
    if(!sketch) f->plane = *ref;
    f->sketch = sketch ? m_sketch : cad::kNoFeature;
    f->curves = sketch ? m_curves : std::vector<int>{};
    f->bodies.clear();
    for(const auto &b : m_bodies) f->bodies.push_back(b.body);
    f->keep = sketch ? cad::SplitKeep::Both : kKeeps[std::clamp(m_keep->currentIndex(), 0, 2)];
    f->pins = pins;
    f->pinDiameter = {m_pinDiameter.name, m_pinDiameterField->expression().toStdString()};
    f->pinDepth = {m_pinDepth.name, m_pinDepthField->expression().toStdString()};
    return f;
}

} // namespace cadjitsu
