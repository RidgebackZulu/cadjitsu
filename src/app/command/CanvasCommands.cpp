#include "command/CanvasCommands.h"

#include "model/CanvasPicture.h"
#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include <QCheckBox>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace cadjitsu {

namespace {

const QColor kInput(20, 100, 225);
const QColor kMark(229, 70, 58);

QString lengthText(double v) { return QString::fromStdString(SketchEditor::formatExpression(v, cad::ValueKind::Length)); }

QVector3D onFrame(const gp_Ax3 &f, cad::Vec2 p) {
    const gp_XYZ w = f.Location().XYZ() + f.XDirection().XYZ() * p.x + f.YDirection().XYZ() * p.y;
    return QVector3D(float(w.X()), float(w.Y()), float(w.Z()));
}

} // namespace

// --- CanvasCommand -------------------------------------------------------------------

CanvasCommand::CanvasCommand(const CommandContext &ctx, int canvas, const std::string &imageKey, QSize pixels)
    : Command(ctx, cad::kNoFeature), m_canvas(canvas) {
    if(const cad::ReferenceImage *c = ctx.doc->canvas(canvas)) {
        m_base = *c;
    } else {
        m_canvas = 0;
        m_base.imageKey = imageKey;
        m_base.pixelWidth = pixels.width();
        m_base.pixelHeight = pixels.height();
        m_base.plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        m_base.mmPerPixel = pixels.width() > 0 ? 100.0 / pixels.width() : 0.1; // 100 mm wide to start with
    }
}

IconId CanvasCommand::iconId() const { return IconId::Canvas; }

void CanvasCommand::setup() {
    CommandPanel &panel = *m_ctx.panel;
    m_planeField = panel.addSelection(tr("Plane"), tr("Select a plane or face"), "canvasPlane");
    m_width = panel.addValue(tr("Width"), cad::ValueKind::Length, evaluator(), "canvasWidth");
    m_x = panel.addValue(tr("X"), cad::ValueKind::Length, evaluator(), "canvasX");
    m_y = panel.addValue(tr("Y"), cad::ValueKind::Length, evaluator(), "canvasY");
    m_rotation = panel.addAngle(tr("Rotation"), evaluator(), "canvasRotation", kMark);
    m_flip = panel.addCheck(tr("Flip"), "canvasFlip");
    m_opacity = panel.addValue(tr("Opacity %"), cad::ValueKind::Scalar, evaluator(), "canvasOpacity");
    m_x->setToolTip(tr("Where the picture's middle is on the plane"));
    m_opacity->setToolTip(tr("0: invisible, 100: solid"));
    m_plane = InputRef::ofPlane(m_base.plane);
    m_width->setExpression(lengthText(m_base.width()));
    m_x->setExpression(lengthText(m_base.origin.x));
    m_y->setExpression(lengthText(m_base.origin.y));
    m_rotation->setExpression(
        QString::fromStdString(SketchEditor::formatExpression(m_base.rotation * cad::kPi / 180.0, cad::ValueKind::Angle)));
    m_flip->setChecked(m_base.flip);
    m_opacity->setExpression(QString::number(std::lround(m_base.opacity * 100)));
    for(ValueField *v : {m_width, m_x, m_y, m_rotation, m_opacity})
        connect(v, &ValueField::edited, this, &Command::inputsChanged);
    connect(m_flip, &QCheckBox::toggled, this, &Command::inputsChanged);
    connect(m_planeField, &SelectionField::cleared, this, [this] {
        m_plane.reset();
        emit inputsChanged();
    });
    SelectFilter sf;
    sf.edges = sf.vertices = sf.bodies = sf.profiles = false;
    sf.planes = true;
    sf.planarFacesOnly = true;
    m_ctx.view->setFilter(sf);
    m_ctx.view->setOriginForced(true);
    m_planeField->setActive(true);
    m_width->setFocus(Qt::OtherFocusReason);
    m_width->selectAll();
}

std::optional<cad::ReferenceImage> CanvasCommand::current() const {
    const std::optional<cad::PlaneRef> ref = m_plane ? m_plane->planeRef() : std::nullopt;
    if(!ref) return std::nullopt;
    for(ValueField *v : {m_width, m_x, m_y, m_rotation, m_opacity})
        if(!v->valid()) return std::nullopt;
    if(!(*m_width->value() > 0) || m_base.pixelWidth <= 0) return std::nullopt;
    cad::ReferenceImage c = m_base;
    c.id = m_canvas;
    c.plane = *ref;
    c.mmPerPixel = *m_width->value() / m_base.pixelWidth;
    c.origin = {*m_x->value(), *m_y->value()};
    c.rotation = *m_rotation->value() * 180.0 / cad::kPi;
    c.flip = m_flip->isChecked();
    c.opacity = std::clamp(*m_opacity->value() / 100.0, 0.0, 1.0);
    c.visible = true;
    return c;
}

bool CanvasCommand::ready(QString &why) {
    if(!m_plane) {
        why = tr("Select the plane or planar face for the picture.");
        return false;
    }
    if(!current()) {
        why = tr("Width, X, Y, rotation and opacity: enter values (the width more than 0).");
        return false;
    }
    return true;
}

void CanvasCommand::showPreview() {
    std::optional<cad::ReferenceImage> c = current();
    if(!c && m_canvas) c = m_base; // keep showing it while a value is being typed
    m_ctx.view->setCanvasOverride(c);
    ModelView::InputMarks marks;
    if(m_plane && m_ctx.view->state()) markInput(*m_ctx.view, *m_ctx.view->state(), *m_plane, -1, kInput, marks);
    m_ctx.view->setInputMarks(std::move(marks));
    m_planeField->setCount(m_plane ? 1 : 0);
}

void CanvasCommand::apply() {
    const std::optional<cad::ReferenceImage> c = current();
    if(!c) return;
    if(m_canvas) m_ctx.doc->updateCanvas(*c);
    else m_canvas = m_ctx.doc->addCanvas(*c);
}

void CanvasCommand::end() { m_ctx.view->setCanvasOverride(std::nullopt); }

void CanvasCommand::picked(const std::optional<SelectionItem> &item, const PickHit &, Qt::KeyboardModifiers) {
    if(!item) return;
    const std::optional<cad::PlaneRef> p = m_ctx.view->planeRefOf(*item);
    if(!p) return;
    m_plane = InputRef::ofPlane(*p);
    emit inputsChanged();
}

// --- CanvasPointsTool ----------------------------------------------------------------

std::optional<cad::Vec2> CanvasPointsTool::onPlane(QPointF px) const {
    if(!hasFrame) return std::nullopt;
    QVector3D o, d;
    m_vp->camera().ray(px, o, d);
    const gp_Dir n = frame.Direction();
    const QVector3D nq(float(n.X()), float(n.Y()), float(n.Z()));
    const gp_Pnt c = frame.Location();
    const QVector3D cq(float(c.X()), float(c.Y()), float(c.Z()));
    const float den = QVector3D::dotProduct(d, nq);
    if(std::fabs(den) < 1e-6f) return std::nullopt;
    const float t = QVector3D::dotProduct(cq - o, nq) / den;
    const QVector3D w = o + d * t;
    const gp_XYZ rel(w.x() - c.X(), w.y() - c.Y(), w.z() - c.Z());
    return cad::Vec2(rel.Dot(frame.XDirection().XYZ()), rel.Dot(frame.YDirection().XYZ()));
}

bool CanvasPointsTool::mousePress(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton) return false;
    m_pressed = true;
    return true;
}

bool CanvasPointsTool::mouseMove(QMouseEvent *e) {
    m_hover = onPlane(e->position());
    m_vp->refreshOverlay();
    m_vp->update();
    return false;
}

bool CanvasPointsTool::mouseRelease(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton || !m_pressed) return false;
    m_pressed = false;
    const std::optional<cad::Vec2> p = onPlane(e->position());
    if(!p) return true;
    if(int(points.size()) >= maxPoints) points.clear(); // start over
    points.push_back(*p);
    if(onChanged) onChanged();
    m_vp->update();
    return true;
}

void CanvasPointsTool::contribute(RenderScene &scene) {
    if(!hasFrame) return;
    PointBatch pb;
    pb.color = kMark;
    pb.outline = Qt::white;
    pb.size = 9.0f;
    pb.depthTest = false;
    LineBatch lb;
    lb.color = kMark;
    lb.width = 2.0f;
    lb.depthTest = false;
    for(size_t i = 0; i < points.size(); ++i) {
        pb.points.push_back(onFrame(frame, points[i]));
        if(i > 0) lb.segments.insert(lb.segments.end(), {onFrame(frame, points[i - 1]), onFrame(frame, points[i])});
    }
    if(closed && int(points.size()) == maxPoints)
        lb.segments.insert(lb.segments.end(), {onFrame(frame, points.back()), onFrame(frame, points.front())});
    else if(m_hover && !points.empty() && int(points.size()) < maxPoints)
        lb.segments.insert(lb.segments.end(), {onFrame(frame, points.back()), onFrame(frame, *m_hover)});
    if(!pb.points.empty()) scene.points.push_back(pb);
    if(!lb.segments.empty()) scene.lines.push_back(lb);
}

void CanvasPointsTool::paintOverlay(QPainter &p) {
    if(!hasFrame) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setBold(true);
    p.setFont(f);
    for(size_t i = 0; i < points.size(); ++i) {
        const QPointF s = m_vp->camera().project(onFrame(frame, points[i]));
        const QRectF r(s + QPointF(8, -22), QSizeF(18, 16));
        p.setPen(Qt::NoPen);
        p.setBrush(kMark);
        p.drawRoundedRect(r, 4, 4);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, QString::number(i + 1));
    }
    p.restore();
}

// --- CanvasCalibrateCommand ----------------------------------------------------------

CanvasCalibrateCommand::CanvasCalibrateCommand(const CommandContext &ctx, int canvas, Mode mode)
    : Command(ctx, cad::kNoFeature), m_canvas(canvas), m_mode(mode), m_tool(ctx.viewport) {}

IconId CanvasCalibrateCommand::iconId() const { return IconId::Canvas; }

QString CanvasCalibrateCommand::prompt() const {
    return m_mode == Mode::Scale
               ? tr("Click two marks on the picture a known distance apart (a ruler, a coin, a caliper's jaws), then "
                    "type the real distance.")
               : tr("Click the four corners of something rectangular in the picture (a sheet of paper, a cutting "
                    "mat), then type its real width and height.");
}

cad::ReferenceImage CanvasCalibrateCommand::picking() const {
    cad::ReferenceImage c;
    if(const cad::ReferenceImage *doc = m_ctx.doc->canvas(m_canvas)) c = *doc;
    c.visible = true;
    if(m_mode == Mode::Perspective && c.perspective) {
        // The photo as taken, as wide as the corrected picture: the corners
        // are picked on the original.
        const QImage photo = canvasPhoto(*m_ctx.doc, c);
        if(!photo.isNull()) {
            c.mmPerPixel = c.width() / photo.width();
            c.pixelWidth = photo.width();
            c.pixelHeight = photo.height();
        }
        c.perspective.reset();
    }
    return c;
}

void CanvasCalibrateCommand::setup() {
    CommandPanel &panel = *m_ctx.panel;
    if(m_mode == Mode::Scale) {
        m_pointsField = panel.addSelection(tr("Points"), tr("Click two marks"), "calibratePoints");
        m_distance = panel.addValue(tr("Real Distance"), cad::ValueKind::Length, evaluator(), "calibrateDistance");
        connect(m_distance, &ValueField::edited, this, &Command::inputsChanged);
        m_tool.maxPoints = 2;
    } else {
        m_pointsField = panel.addSelection(tr("Corners"), tr("Click four corners"), "perspectiveCorners");
        m_width = panel.addValue(tr("Real Width"), cad::ValueKind::Length, evaluator(), "perspectiveWidth");
        m_height = panel.addValue(tr("Real Height"), cad::ValueKind::Length, evaluator(), "perspectiveHeight");
        // An A4 sheet, the usual thing to photograph a part on.
        m_width->setExpression(QStringLiteral("210 mm"));
        m_height->setExpression(QStringLiteral("297 mm"));
        if(const cad::ReferenceImage *c = m_ctx.doc->canvas(m_canvas); c && c->perspective) {
            m_width->setExpression(lengthText(c->perspective->realWidth));
            m_height->setExpression(lengthText(c->perspective->realHeight));
        }
        for(ValueField *v : {m_width, m_height}) connect(v, &ValueField::edited, this, &Command::inputsChanged);
        m_tool.maxPoints = 4;
        m_tool.closed = true;
    }
    m_pointsField->setActive(true);
    connect(m_pointsField, &SelectionField::cleared, this, [this] {
        m_tool.points.clear();
        emit inputsChanged();
    });
    m_tool.onChanged = [this] {
        // Two marks picked: offer what they measure now, to type over.
        if(m_mode == Mode::Scale && m_tool.points.size() == 2) {
            m_distance->setExpression(lengthText(cad::distance(m_tool.points[0], m_tool.points[1])));
            m_distance->setFocus(Qt::OtherFocusReason);
            m_distance->selectAll();
        }
        emit inputsChanged();
    };
    SelectFilter none;
    none.faces = none.edges = none.vertices = none.bodies = none.profiles = false;
    m_ctx.view->setFilter(none);
}

void CanvasCalibrateCommand::addPoint(cad::Vec2 p) {
    if(int(m_tool.points.size()) >= m_tool.maxPoints) m_tool.points.clear();
    m_tool.points.push_back(p);
    if(m_tool.onChanged) m_tool.onChanged();
}

std::optional<QVector3D> CanvasCalibrateCommand::canvasAnchor() const {
    if(m_tool.points.empty() || !m_tool.hasFrame) return m_lastPick;
    return onFrame(m_tool.frame, m_tool.points.back());
}

bool CanvasCalibrateCommand::ready(QString &why) {
    if(!m_ctx.doc->canvas(m_canvas)) {
        why = tr("The canvas is gone.");
        return false;
    }
    const size_t need = m_mode == Mode::Scale ? 2 : 4;
    if(m_tool.points.size() < need) {
        why = m_mode == Mode::Scale ? tr("Click two marks on the picture (%1 of 2).").arg(m_tool.points.size())
                                    : tr("Click the four corners (%1 of 4).").arg(m_tool.points.size());
        return false;
    }
    if(m_mode == Mode::Scale) {
        if(!m_distance->valid() || !(*m_distance->value() > 0)) {
            why = tr("Type the real distance between the marks.");
            return false;
        }
    } else if(!m_width->valid() || !m_height->valid() || !(*m_width->value() > 0) || !(*m_height->value() > 0)) {
        why = tr("Type the rectangle's real width and height.");
        return false;
    }
    return true;
}

void CanvasCalibrateCommand::showPreview() {
    const cad::ReferenceImage c = picking();
    m_ctx.view->setCanvasOverride(c);
    m_tool.hasFrame = m_ctx.view->canvasFrame(c, m_tool.frame);
    m_pointsField->setCount(int(m_tool.points.size()));
    m_ctx.viewport->update();
}

void CanvasCalibrateCommand::apply() {
    const cad::ReferenceImage *doc = m_ctx.doc->canvas(m_canvas);
    if(!doc) return;
    cad::ReferenceImage c = *doc;
    if(m_mode == Mode::Scale) {
        std::string why;
        if(!cad::calibrateReferenceImage(c, m_tool.points[0], m_tool.points[1], *m_distance->value(), why)) return;
        m_ctx.doc->updateCanvas(c, true, "Calibrate " + c.name);
        return;
    }
    // Perspective: the corners in the photo's pixels.
    const cad::ReferenceImage shown = picking();
    std::array<cad::Vec2, 4> corners;
    for(size_t k = 0; k < 4; ++k) corners[k] = shown.toPixel(m_tool.points[k]);
    if(!applyPerspective(*m_ctx.doc, c, corners, *m_width->value(), *m_height->value())) return;
    m_ctx.doc->updateCanvas(c, true, "Correct Perspective of " + c.name);
}

void CanvasCalibrateCommand::end() { m_ctx.view->setCanvasOverride(std::nullopt); }

} // namespace cadjitsu
