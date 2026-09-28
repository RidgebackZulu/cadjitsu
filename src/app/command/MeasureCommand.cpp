#include "command/MeasureCommand.h"

#include "model/ModelView.h"
#include "ui/Icons.h"
#include "viewport/OverlayPaint.h"
#include "viewport/RenderScene.h"
#include "viewport/Viewport.h"

#include "geom/OcctUtil.h"
#include "measure/Measure.h"

#include <BRepBuilderAPI_MakeVertex.hxx>
#include <TopoDS.hxx>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSettings>

#include <cmath>

namespace cadjitsu {

namespace {

const QColor kDim(26, 101, 201);
const QColor kCentre(222, 124, 22);
const QColor kPicked(20, 100, 225);
const QColor kAxis[3] = {QColor(229, 70, 58), QColor(61, 174, 79), QColor(47, 110, 230)};
const char *kUnitsKey = "measure/units";

QVector3D toQ(const gp_Pnt &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); }

Camera cameraOf(const Viewport *vp) {
    Camera c = vp->camera();
    c.viewport = vp->size();
    return c;
}

} // namespace

// --- overlay -----------------------------------------------------------------------------

void MeasureOverlay::contribute(RenderScene &scene) {
    auto batch = [](const QColor &c, float width) {
        LineBatch b;
        b.color = c;
        b.width = width;
        b.depthTest = false;
        b.ignoreClip = true;
        return b;
    };
    if(distance) {
        LineBatch main = batch(kDim, 2.2f);
        main.segments = {distance->a, distance->b};
        scene.lines.push_back(main);
        if(showLegs) {
            // X, then Y, then Z from the first point to the second.
            QVector3D p = distance->a;
            for(int k = 0; k < 3; ++k) {
                QVector3D q = p;
                q[k] = distance->b[k];
                if((q - p).length() > 1e-6f) {
                    LineBatch leg = batch(kAxis[k], 1.2f);
                    leg.segments = {p, q};
                    scene.lines.push_back(leg);
                }
                p = q;
            }
        }
    }
    if(centres) {
        LineBatch c = batch(kCentre, 1.6f);
        c.segments = {centres->a, centres->b};
        scene.lines.push_back(c);
    }
    PointBatch pts;
    pts.color = kPicked;
    pts.outline = QColor(255, 255, 255);
    pts.size = 9.0f;
    pts.round = true;
    pts.depthTest = false;
    pts.points = points;
    if(distance) pts.points.insert(pts.points.end(), {distance->a, distance->b});
    if(!pts.points.empty()) scene.points.push_back(pts);
}

void MeasureOverlay::paintOverlay(QPainter &p) {
    const Camera cam = cameraOf(m_viewport);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    auto labelled = [&](const Line &l, const QColor &color, bool arrows, bool below) {
        const QPointF a = cam.project(l.a), b = cam.project(l.b);
        if(arrows && QLineF(a, b).length() > 24.0) {
            drawArrow(p, a, a - b, color);
            drawArrow(p, b, b - a, color);
        }
        // The value beside the middle of the line, off it to one side.
        QPointF mid = (a + b) / 2.0, n(0, -1);
        const double len = QLineF(a, b).length();
        if(len > 1.0) {
            n = QPointF(-(b - a).y() / len, (b - a).x() / len);
            if(n.y() > 0) n = -n;
        }
        if(below) n = -n;
        drawLabelPlate(p, mid + n * 16.0, l.label, color, QColor(16, 34, 60));
    };
    // The centre distance goes on the other side, so collinear lines keep both values readable.
    if(centres) labelled(*centres, kCentre, false, distance.has_value());
    if(distance) labelled(*distance, kDim, true, false);
    p.restore();
}

// --- command -----------------------------------------------------------------------------

MeasureCommand::MeasureCommand(const CommandContext &ctx) : Command(ctx, cad::kNoFeature), m_overlay(ctx.viewport) {}

IconId MeasureCommand::iconId() const { return IconId::Measure; }

QString MeasureCommand::formatLength(double mm, bool inches) {
    if(inches) return QStringLiteral("%1 in").arg(mm / 25.4, 0, 'f', 4);
    return QStringLiteral("%1 mm").arg(mm, 0, 'f', 3);
}

void MeasureCommand::setup() {
    CommandPanel &panel = *m_ctx.panel;
    m_mode = panel.addChoice(tr("Select"), {tr("Faces, edges, points"), tr("Bodies"), tr("Points on surfaces")},
                             "measureMode");
    m_units = panel.addChoice(tr("Units"), {tr("Millimetres"), tr("Inches")}, "measureUnits");
    m_units->setCurrentIndex(QSettings().value(QString::fromLatin1(kUnitsKey)).toString() == QLatin1String("in") ? 1 : 0);
    m_first = panel.addInfo(tr("First"), "measureFirst");
    m_second = panel.addInfo(tr("Second"), "measureSecond");
    m_distance = panel.addInfo(tr("Distance"), "measureDistance");
    m_delta = panel.addInfo(tr("ΔX  ΔY  ΔZ"), "measureDelta");
    m_centre = panel.addInfo(tr("Centre to centre"), "measureCentre");
    m_angle = panel.addInfo(tr("Angle"), "measureAngle");
    m_props = panel.addInfo(tr("Properties"), "measureProperties");
    m_copy = panel.addButton(QString(), tr("Copy results"), "measureCopy");
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        clear();
        applyFilter();
    });
    connect(m_units, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QString::fromLatin1(kUnitsKey), m_units->currentIndex() == 1 ? "in" : "mm");
        update();
    });
    connect(m_copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(resultText()); });
    applyFilter();
    // Whatever was selected before counts as the first picks.
    for(const SelectionItem &it : m_ctx.view->selection().items()) {
        if(m_targets.size() == 2) break;
        if(auto t = targetFor(it, PickHit())) m_targets.push_back(*t);
    }
    update();
}

void MeasureCommand::applyFilter() {
    SelectFilter sf;
    sf.profiles = sf.planes = false;
    const Mode mode = Mode(std::max(0, m_mode->currentIndex()));
    sf.faceSelectsBody = mode == Mode::Bodies;
    sf.edges = sf.vertices = mode == Mode::Entities;
    sf.sketchPoints = mode != Mode::Bodies;
    m_ctx.view->setFilter(sf);
}

void MeasureCommand::end() { m_ctx.view->setInputMarks({}); }

std::optional<MeasureCommand::Target> MeasureCommand::targetFor(const SelectionItem &item, const PickHit &hit) const {
    const cad::StatePtr st = m_ctx.view->state();
    if(!st) return std::nullopt;
    const Mode mode = Mode(std::max(0, m_mode->currentIndex()));
    Target t;
    if(item.kind == SelectionItem::Kind::SketchEntity) {
        const std::optional<gp_Pnt> p = m_ctx.view->sketchPointOf(item);
        if(!p) return std::nullopt;
        t.shape = BRepBuilderAPI_MakeVertex(*p).Shape();
        t.point = toQ(*p);
        t.name = tr("Sketch point");
        return t;
    }
    const cad::Body *b = st->body(item.body);
    if(!b) return std::nullopt;
    const QString body = QString::fromStdString(b->name);
    if(mode == Mode::Points) {
        if(!hit.valid()) return std::nullopt;
        const gp_Pnt p(hit.point.x(), hit.point.y(), hit.point.z());
        t.shape = BRepBuilderAPI_MakeVertex(p).Shape();
        t.point = hit.point;
        t.name = tr("Point on %1").arg(body);
        return t;
    }
    t.ref = inputRefOf(*m_ctx.view, item);
    switch(item.kind) {
    case SelectionItem::Kind::Body:
        t.shape = b->shape.shape();
        t.name = body;
        break;
    case SelectionItem::Kind::Face:
        t.shape = b->shape.face(item.index);
        t.name = tr("%1 face (%2)").arg(QString::fromStdString(cad::surfaceTypeName(TopoDS::Face(t.shape))), body);
        break;
    case SelectionItem::Kind::Edge:
        t.shape = b->shape.edge(item.index);
        t.name = tr("Edge of %1").arg(body);
        break;
    case SelectionItem::Kind::Vertex:
        t.shape = b->shape.vertex(item.index);
        t.name = tr("Vertex of %1").arg(body);
        break;
    default:
        return std::nullopt;
    }
    if(t.shape.IsNull()) return std::nullopt;
    return t;
}

void MeasureCommand::picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers) {
    if(!item) { // empty space: start again
        clear();
        return;
    }
    std::optional<Target> t = targetFor(*item, hit);
    if(!t) return;
    // Clicking a pick again drops it; a third pick starts a new measurement.
    for(size_t i = 0; i < m_targets.size(); ++i)
        if(!t->point && m_targets[i].ref && t->ref && *m_targets[i].ref == *t->ref) {
            m_targets.erase(m_targets.begin() + long(i));
            update();
            return;
        }
    if(m_targets.size() == 2) m_targets.clear();
    m_targets.push_back(std::move(*t));
    update();
}

void MeasureCommand::clear() {
    m_targets.clear();
    update();
}

void MeasureCommand::update() {
    const bool inches = m_units->currentIndex() == 1;
    auto len = [&](double v) { return formatLength(v, inches); };
    m_result.reset();
    m_overlay.distance.reset();
    m_overlay.centres.reset();
    m_overlay.points.clear();
    m_overlay.showLegs = false;

    m_first->setText(m_targets.size() > 0 ? m_targets[0].name : tr("Click something to measure"));
    m_second->setText(m_targets.size() > 1 ? m_targets[1].name : QString());
    CommandPanel &panel = *m_ctx.panel;
    panel.setRowVisible(m_second, m_targets.size() > 0);
    for(const Target &t : m_targets)
        if(t.point) m_overlay.points.push_back(*t.point);

    bool two = false, centre = false, angle = false;
    if(m_targets.size() == 2) {
        m_result = cad::measureBetween(m_targets[0].shape, m_targets[1].shape);
        if(m_result->ok) {
            two = true;
            const cad::MeasureResult &r = *m_result;
            m_distance->setText(len(r.distance));
            const gp_Vec d(r.p1, r.p2);
            const int places = inches ? 4 : 3;
            m_delta->setText(QStringLiteral("%1,  %2,  %3 %4")
                                 .arg(std::fabs(d.X()) / (inches ? 25.4 : 1.0), 0, 'f', places)
                                 .arg(std::fabs(d.Y()) / (inches ? 25.4 : 1.0), 0, 'f', places)
                                 .arg(std::fabs(d.Z()) / (inches ? 25.4 : 1.0), 0, 'f', places)
                                 .arg(inches ? QStringLiteral("in") : QStringLiteral("mm")));
            if(r.distance > 1e-9) {
                m_overlay.distance = MeasureOverlay::Line{toQ(r.p1), toQ(r.p2), len(r.distance)};
                // The legs only help when the line is not along an axis.
                int nonZero = 0;
                for(double c : {d.X(), d.Y(), d.Z()}) nonZero += std::fabs(c) > 1e-6;
                m_overlay.showLegs = nonZero > 1;
            }
            if(r.centreDistance) {
                centre = true;
                m_centre->setText(len(*r.centreDistance));
                if(*r.centreDistance > 1e-9 && std::fabs(*r.centreDistance - r.distance) > 1e-6)
                    m_overlay.centres = MeasureOverlay::Line{toQ(r.c1), toQ(r.c2), tr("⌀-⌀ %1").arg(len(*r.centreDistance))};
            }
            if(r.angle) {
                angle = true;
                m_angle->setText(QStringLiteral("%1°").arg(*r.angle * 180.0 / M_PI, 0, 'f', 2));
            }
        } else {
            m_distance->setText(QString::fromStdString(m_result->error));
            two = true;
        }
    }
    // One pick: its own size.
    QString props;
    if(m_targets.size() == 1) {
        const TopoDS_Shape &s = m_targets[0].shape;
        std::vector<cad::Measurement> m;
        switch(s.ShapeType()) {
        case TopAbs_FACE: m = cad::measureFace(TopoDS::Face(s)); break;
        case TopAbs_EDGE: m = cad::measureEdge(TopoDS::Edge(s)); break;
        case TopAbs_VERTEX: m = cad::measureVertex(TopoDS::Vertex(s)); break;
        default: m = cad::measureBody(s); break;
        }
        QStringList lines;
        for(const cad::Measurement &x : m) {
            if(inches && x.unit == cad::MeasureUnit::Length) {
                lines << QString::fromStdString(x.label) + QStringLiteral(": ") + len(x.value);
            } else if(inches && x.unit == cad::MeasureUnit::Area) {
                lines << QString::fromStdString(x.label) + QStringLiteral(": %1 in²").arg(x.value / (25.4 * 25.4), 0, 'f', 4);
            } else if(inches && x.unit == cad::MeasureUnit::Volume) {
                lines << QString::fromStdString(x.label) +
                             QStringLiteral(": %1 in³").arg(x.value / (25.4 * 25.4 * 25.4), 0, 'f', 4);
            } else {
                lines << QString::fromStdString(x.format(3));
            }
        }
        props = lines.join(QLatin1Char('\n'));
    }
    m_props->setText(props);
    panel.setRowVisible(m_props, !props.isEmpty());
    panel.setRowVisible(m_distance, two);
    panel.setRowVisible(m_delta, two && m_result && m_result->ok);
    panel.setRowVisible(m_centre, centre);
    panel.setRowVisible(m_angle, angle);
    m_copy->setEnabled(!m_targets.empty());

    // Highlight the picks.
    ModelView::InputMarks marks;
    if(const cad::StatePtr st = m_ctx.view->state())
        for(size_t i = 0; i < m_targets.size(); ++i)
            if(m_targets[i].ref) markInput(*m_ctx.view, *st, *m_targets[i].ref, -1, kPicked, marks);
    m_ctx.view->setInputMarks(std::move(marks));
    m_ctx.panel->setMessage(m_targets.size() == 1 ? tr("Click a second thing to measure the distance to it.") : QString());
    m_ctx.viewport->refreshOverlay();
    m_ctx.viewport->update();
}

QString MeasureCommand::resultText() const {
    QStringList out;
    const std::pair<QString, QLabel *> rows[] = {{tr("First"), m_first},       {tr("Second"), m_second},
                                                 {tr("Distance"), m_distance}, {tr("dX dY dZ"), m_delta},
                                                 {tr("Centre to centre"), m_centre}, {tr("Angle"), m_angle},
                                                 {tr("Properties"), m_props}};
    for(const auto &[name, label] : rows)
        if(label->isVisibleTo(m_ctx.panel) && !label->text().isEmpty())
            out << name + QStringLiteral(": ") + QString(label->text()).replace(QLatin1Char('\n'), QStringLiteral("; "));
    return out.join(QLatin1Char('\n'));
}

} // namespace cadjitsu
