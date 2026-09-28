#include "command/HoleCommand.h"

#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "ui/Icons.h"

#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <QComboBox>
#include <QLabel>

#include <cmath>

namespace cadjitsu {

namespace {

const QColor kInput(20, 100, 225);
const cad::HoleType kTypes[] = {cad::HoleType::Simple, cad::HoleType::Counterbore, cad::HoleType::Countersink,
                                cad::HoleType::Tapped};
const cad::ThreadMode kThreadModes[] = {cad::ThreadMode::Auto, cad::ThreadMode::Modeled, cad::ThreadMode::TapDrill};

double round2(double v) { return std::round(v * 100.0) / 100.0; }

QString format(double v) { return QString::fromStdString(SketchEditor::formatExpression(v, cad::ValueKind::Length)); }

} // namespace

IconId HoleCommand::iconId() const { return IconId::Hole; }

bool HoleCommand::atSketchPoints() const { return m_placement && m_placement->currentIndex() == 1; }

void HoleCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::HoleFeature>(doc.feature(m_editing));
    m_placement = panel.addChoice(tr("Placement"), {tr("On Face"), tr("At Sketch Points")}, "holePlacement");
    m_position = panel.addSelection(tr("Position"), tr("Click a planar face"), "holePosition");
    m_x = panel.addValue(tr("X"), cad::ValueKind::Length, evaluator(), "holeX");
    m_y = panel.addValue(tr("Y"), cad::ValueKind::Length, evaluator(), "holeY");
    m_type = panel.addChoice(tr("Hole Type"), {tr("Simple"), tr("Counterbore"), tr("Countersink"), tr("Tapped")}, "holeType");
    QStringList sizes;
    for(const cad::ThreadSpec &t : cad::threadTable()) sizes << QString::fromStdString(t.name);
    m_threadSize = panel.addChoice(tr("Thread"), sizes, "holeThread");
    m_threadSize->setCurrentText(QStringLiteral("M3"));
    m_threadMode = panel.addChoice(tr("Thread Type"), {tr("By size (M5 up modelled)"), tr("Modelled"), tr("Tap drill only")},
                                   "holeThreadMode");
    m_threadMode->setToolTip(tr("Small threads print poorly: by default M4 / #8 and smaller are left as a tap drill "
                                "bore, to tap, or for a self-tapping screw or a heat-set insert."));
    m_threadClearanceField = panel.addValue(tr("Thread Clearance"), cad::ValueKind::Length, evaluator(), "holeThreadClearance");
    m_threadInfo = panel.addInfo(QString(), "holeThreadInfo");
    m_threadInfo->setStyleSheet(QStringLiteral("color: #3c4450; font-weight: normal;"));
    m_extent = panel.addChoice(tr("Extents"), {tr("Distance"), tr("All")}, "holeExtent");
    m_diameterField = panel.addValue(tr("Diameter"), cad::ValueKind::Length, evaluator(), "holeDiameter");
    m_depthField = panel.addValue(tr("Depth"), cad::ValueKind::Length, evaluator(), "holeDepth");
    m_cboreDiameterField = panel.addValue(tr("Counterbore Diameter"), cad::ValueKind::Length, evaluator(), "holeCboreDiameter");
    m_cboreDepthField = panel.addValue(tr("Counterbore Depth"), cad::ValueKind::Length, evaluator(), "holeCboreDepth");
    m_csinkDiameterField = panel.addValue(tr("Countersink Diameter"), cad::ValueKind::Length, evaluator(), "holeCsinkDiameter");
    m_csinkAngleField = panel.addValue(tr("Countersink Angle"), cad::ValueKind::Angle, evaluator(), "holeCsinkAngle");
    m_tip = panel.addChoice(tr("Drill Point"), {tr("Angle"), tr("Flat")}, "holeTip");
    m_tipAngleField = panel.addValue(tr("Point Angle"), cad::ValueKind::Angle, evaluator(), "holeTipAngle");

    const cad::HoleFeature *o = m_original.get();
    auto slot = [&](const cad::ParamSlot *s, const char *fallback) {
        return s && !s->empty() ? *s : doc.makeSlot(fallback);
    };
    m_diameter = slot(o ? &o->diameter : nullptr, "5 mm");
    m_depth = slot(o ? &o->depth : nullptr, "10 mm");
    m_cboreDiameter = slot(o ? &o->cboreDiameter : nullptr, "9 mm");
    m_cboreDepth = slot(o ? &o->cboreDepth : nullptr, "3 mm");
    m_csinkDiameter = slot(o ? &o->csinkDiameter : nullptr, "9 mm");
    m_csinkAngle = slot(o ? &o->csinkAngle : nullptr, "90 deg");
    m_tipAngle = slot(o ? &o->tipAngle : nullptr, "118 deg");
    m_threadClearance = slot(o ? &o->threadClearance : nullptr, "0.15 mm");
    m_threadClearanceField->setExpression(QString::fromStdString(m_threadClearance.expr));
    connect(m_threadClearanceField, &ValueField::edited, this, &Command::inputsChanged);
    if(o && o->holeType == cad::HoleType::Tapped) {
        m_threadSize->setCurrentText(QString::fromStdString(o->thread));
        for(int i = 0; i < 3; ++i)
            if(kThreadModes[i] == o->threadMode) m_threadMode->setCurrentIndex(i);
    }
    const std::pair<ValueField *, const cad::ParamSlot *> values[] = {
        {m_diameterField, &m_diameter},           {m_depthField, &m_depth},
        {m_cboreDiameterField, &m_cboreDiameter}, {m_cboreDepthField, &m_cboreDepth},
        {m_csinkDiameterField, &m_csinkDiameter}, {m_csinkAngleField, &m_csinkAngle},
        {m_tipAngleField, &m_tipAngle}};
    for(const auto &[field, s] : values) {
        field->setExpression(QString::fromStdString(s->expr));
        connect(field, &ValueField::edited, this, &Command::inputsChanged);
    }
    if(o) {
        if(!o->face.empty()) {
            m_face = InputRef::ofTopo(o->face);
            m_points = o->points;
        } else {
            m_placement->setCurrentIndex(1);
            for(int p : o->sketchPoints) m_sketchPoints.push_back(InputRef::ofSketchPoint(o->sketch, p));
        }
        for(int i = 0; i < 4; ++i)
            if(kTypes[i] == o->holeType) m_type->setCurrentIndex(i);
        m_extent->setCurrentIndex(o->extent == cad::ExtentType::ThroughAll ? 1 : 0);
        m_tip->setCurrentIndex(o->flatTip ? 1 : 0);
    } else {
        // Sketch points selected beforehand, as in Fusion.
        for(const auto &it : m_ctx.view->selection().items())
            if(it.kind == SelectionItem::Kind::SketchEntity)
                if(const auto r = inputRefOf(*m_ctx.view, it)) m_sketchPoints.push_back(*r);
        if(!m_sketchPoints.empty()) m_placement->setCurrentIndex(1);
    }
    connect(m_placement, &QComboBox::currentIndexChanged, this, [this] {
        usePlacementFilter();
        updateRows();
        updateMarks();
        emit inputsChanged();
    });
    for(QComboBox *c : {m_type, m_extent, m_tip, m_threadSize, m_threadMode})
        connect(c, &QComboBox::currentIndexChanged, this, [this] {
            updateRows();
            emit inputsChanged();
        });
    connect(m_position, &SelectionField::cleared, this, [this] {
        m_face.reset();
        m_points.clear();
        m_sketchPoints.clear();
        updateMarks();
        updateRows();
        emit inputsChanged();
    });
    // X / Y move the last hole placed on the face.
    for(ValueField *v : {m_x, m_y})
        connect(v, &ValueField::edited, this, [this] {
            if(m_points.empty() || !m_x->valid() || !m_y->valid()) return;
            m_points.back() = cad::Vec2{*m_x->value(), *m_y->value()};
            updateMarks();
            emit inputsChanged();
        });
    m_position->setActive(true);
    usePlacementFilter();
    updateRows();
    updateMarks();
    showPosition();
}

void HoleCommand::usePlacementFilter() {
    SelectFilter sf;
    sf.edges = sf.vertices = sf.bodies = sf.profiles = false;
    if(atSketchPoints()) {
        sf.faces = false;
        sf.sketchPoints = true;
    } else {
        sf.planarFacesOnly = true;
    }
    m_ctx.view->setFilter(sf);
}

void HoleCommand::updateRows() {
    CommandPanel &panel = *m_ctx.panel;
    const cad::HoleType t = kTypes[std::clamp(m_type->currentIndex(), 0, 3)];
    const bool all = m_extent->currentIndex() == 1;
    const bool onFace = !atSketchPoints();
    panel.setRowVisible(m_x, onFace && !m_points.empty());
    panel.setRowVisible(m_y, onFace && !m_points.empty());
    panel.setRowVisible(m_depthField, !all);
    panel.setRowVisible(m_cboreDiameterField, t == cad::HoleType::Counterbore);
    panel.setRowVisible(m_cboreDepthField, t == cad::HoleType::Counterbore);
    panel.setRowVisible(m_csinkDiameterField, t == cad::HoleType::Countersink);
    panel.setRowVisible(m_csinkAngleField, t == cad::HoleType::Countersink);
    panel.setRowVisible(m_tip, !all);
    panel.setRowVisible(m_tipAngleField, !all && m_tip->currentIndex() == 0);
    const bool tapped = t == cad::HoleType::Tapped;
    panel.setRowVisible(m_diameterField, !tapped);
    panel.setRowVisible(m_threadSize, tapped);
    panel.setRowVisible(m_threadMode, tapped);
    const cad::ThreadSpec *spec = cad::findThread(m_threadSize->currentText().toStdString());
    const bool modeled = tapped && spec &&
                         (m_threadMode->currentIndex() == 1 || (m_threadMode->currentIndex() == 0 && spec->modelByDefault()));
    panel.setRowVisible(m_threadClearanceField, modeled);
    panel.setRowVisible(m_threadInfo, tapped && spec);
    if(tapped && spec)
        m_threadInfo->setText(modeled ? tr("%1: %2 mm tap drill, thread modelled").arg(QString::fromStdString(spec->name))
                                            .arg(spec->tapDrill, 0, 'f', 2)
                                      : tr("%1: %2 mm tap drill, to tap after printing (small threads print poorly)")
                                            .arg(QString::fromStdString(spec->name))
                                            .arg(spec->tapDrill, 0, 'f', 2));
}

std::optional<gp_Ax3> HoleCommand::faceFrame(const cad::ModelState &st) const {
    if(!m_face) return std::nullopt;
    const cad::ResolvedRef r = cad::resolveRef(st, m_face->topo);
    gp_Pln pln;
    if(!r.ok || !cad::planeOfFace(TopoDS::Face(r.shape), pln)) return std::nullopt;
    return cad::frameForPlane(pln);
}

void HoleCommand::updateMarks() {
    ModelView::InputMarks marks;
    const cad::StatePtr base = baseState();
    if(base && atSketchPoints()) {
        for(size_t i = 0; i < m_sketchPoints.size(); ++i)
            markInput(*m_ctx.view, *base, m_sketchPoints[i], int(i), kInput, marks);
    } else if(base && m_face) {
        markInput(*m_ctx.view, *base, *m_face, -1, kInput, marks);
        if(const auto frame = faceFrame(*base))
            for(size_t i = 0; i < m_points.size(); ++i) {
                const gp_XYZ p = frame->Location().XYZ() + frame->XDirection().XYZ() * m_points[i].x +
                                 frame->YDirection().XYZ() * m_points[i].y;
                marks.points.push_back({int(i), QVector3D(float(p.X()), float(p.Y()), float(p.Z()))});
            }
    }
    m_ctx.view->setInputMarks(std::move(marks));
    m_position->setCount(holeCount());
}

void HoleCommand::showPosition() {
    if(m_points.empty()) return;
    m_x->setExpression(format(m_points.back().x));
    m_y->setExpression(format(m_points.back().y));
}

void HoleCommand::picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers) {
    if(!item) return;
    const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
    if(!r || r->createdBy(isEditing() ? m_editing : m_ctx.doc->nextFeatureId())) return;
    if(atSketchPoints()) {
        if(r->kind != SelectionItem::Kind::SketchEntity) return;
        // The points of one sketch.
        if(!m_sketchPoints.empty() && m_sketchPoints.front().feature != r->feature) m_sketchPoints.clear();
        toggleRef(m_sketchPoints, *r);
    } else {
        if(r->kind != SelectionItem::Kind::Face || !hit.valid()) return;
        const cad::StatePtr st = m_ctx.view->state();
        gp_Pln pln;
        const cad::Body *b = st ? st->body(item->body) : nullptr;
        if(!b || !cad::planeOfFace(b->shape.face(item->index), pln)) return;
        // A hole where the face was clicked; another face starts over.
        const gp_Ax3 frame = cad::frameForPlane(pln);
        const gp_Vec v(frame.Location(), gp_Pnt(hit.point.x(), hit.point.y(), hit.point.z()));
        const cad::Vec2 p{round2(v.Dot(gp_Vec(frame.XDirection()))), round2(v.Dot(gp_Vec(frame.YDirection())))};
        if(!m_face || !(*m_face == *r)) {
            m_face = *r;
            m_points.clear();
        }
        m_points.push_back(p);
    }
    updateRows();
    updateMarks();
    showPosition();
    emit inputsChanged();
}

void HoleCommand::markClicked(int tag) {
    if(atSketchPoints()) {
        if(tag < 0 || tag >= int(m_sketchPoints.size())) return;
        m_sketchPoints.erase(m_sketchPoints.begin() + tag);
    } else {
        if(tag < 0 || tag >= int(m_points.size())) return;
        m_points.erase(m_points.begin() + tag);
    }
    updateRows();
    updateMarks();
    showPosition();
    emit inputsChanged();
}

std::set<cad::FeatureId> HoleCommand::sketchesToShow() const {
    std::set<cad::FeatureId> out;
    for(const auto &p : m_sketchPoints) out.insert(p.feature);
    return out;
}

ValueField *HoleCommand::canvasValue() const {
    // A tapped hole's size is its thread: typing on the canvas sets the depth.
    return m_type->currentIndex() == 3 ? m_depthField : m_diameterField;
}

std::shared_ptr<cad::Feature> HoleCommand::build(QString &why) {
    if(atSketchPoints() ? m_sketchPoints.empty() : (!m_face || m_points.empty())) {
        why = atSketchPoints() ? tr("Select the sketch points to drill at.") : tr("Click a planar face to place a hole.");
        return nullptr;
    }
    const cad::HoleType t = kTypes[std::clamp(m_type->currentIndex(), 0, 3)];
    const bool all = m_extent->currentIndex() == 1;
    const bool flat = m_tip->currentIndex() == 1;
    auto bad = [&](ValueField *v, const QString &what) {
        if(v->valid()) return false;
        why = tr("%1: enter a value").arg(what);
        return true;
    };
    if((t != cad::HoleType::Tapped && bad(m_diameterField, tr("Diameter"))) || (!all && bad(m_depthField, tr("Depth"))))
        return nullptr;
    if(t == cad::HoleType::Tapped && bad(m_threadClearanceField, tr("Thread clearance"))) return nullptr;
    if(t == cad::HoleType::Counterbore &&
       (bad(m_cboreDiameterField, tr("Counterbore diameter")) || bad(m_cboreDepthField, tr("Counterbore depth"))))
        return nullptr;
    if(t == cad::HoleType::Countersink &&
       (bad(m_csinkDiameterField, tr("Countersink diameter")) || bad(m_csinkAngleField, tr("Countersink angle"))))
        return nullptr;
    if(!all && !flat && bad(m_tipAngleField, tr("Point angle"))) return nullptr;

    auto f = m_original ? std::static_pointer_cast<cad::HoleFeature>(m_original->clone())
                        : std::make_shared<cad::HoleFeature>();
    if(atSketchPoints()) {
        f->face = cad::TopoRef();
        f->points.clear();
        f->sketch = m_sketchPoints.front().feature;
        f->sketchPoints.clear();
        for(const auto &p : m_sketchPoints) f->sketchPoints.push_back(p.entity);
    } else {
        f->face = m_face->topo;
        f->points = m_points;
        f->sketch = cad::kNoFeature;
        f->sketchPoints.clear();
    }
    f->holeType = t;
    f->extent = all ? cad::ExtentType::ThroughAll : cad::ExtentType::Distance;
    f->flatTip = flat;
    auto take = [](const cad::ParamSlot &s, ValueField *v) { return cad::ParamSlot{s.name, v->expression().toStdString()}; };
    f->diameter = take(m_diameter, m_diameterField);
    f->depth = take(m_depth, m_depthField);
    f->cboreDiameter = take(m_cboreDiameter, m_cboreDiameterField);
    f->cboreDepth = take(m_cboreDepth, m_cboreDepthField);
    f->csinkDiameter = take(m_csinkDiameter, m_csinkDiameterField);
    f->csinkAngle = take(m_csinkAngle, m_csinkAngleField);
    f->tipAngle = take(m_tipAngle, m_tipAngleField);
    if(t == cad::HoleType::Tapped) {
        f->thread = m_threadSize->currentText().toStdString();
        f->threadMode = kThreadModes[std::clamp(m_threadMode->currentIndex(), 0, 2)];
        f->threadClearance = take(m_threadClearance, m_threadClearanceField);
    }
    return f;
}

} // namespace cadjitsu
