#include "command/TextCommand.h"

#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <GeomAPI_IntCS.hxx>
#include <Geom_Line.hxx>
#include <TopoDS.hxx>

#include <QCheckBox>
#include <QComboBox>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>

#include <cmath>

namespace cadjitsu {

namespace {

const QColor kInput(20, 100, 225);
const QColor kRotation(229, 70, 58);
const QColor kOutline(235, 120, 20);
constexpr double kKnobPx = 8.0, kKnobGrabPx = 12.0;

QVector3D toQ(const gp_XYZ &v) { return QVector3D(float(v.X()), float(v.Y()), float(v.Z())); }

Camera cameraOf(const Viewport *vp) {
    Camera c = vp->camera();
    c.viewport = vp->size();
    return c;
}

QString lengthText(double v) { return QString::fromStdString(SketchEditor::formatExpression(v, cad::ValueKind::Length)); }

double roundTo(double v, double step) { return std::round(v / step) * step; }

} // namespace

// --- the canvas controls ------------------------------------------------------------------

TextGizmo::TextGizmo(Viewport *viewport) : m_viewport(viewport), m_plane(viewport) {
    m_plane.ring(0).color = kRotation;
    m_plane.ring(0).name = QObject::tr("Rotation");
}

QVector3D TextGizmo::knobPoint() const { return m_frame ? toQ(m_frame->point(m_x, m_y).XYZ()) : QVector3D(); }

QPointF TextGizmo::knobOnScreen() const { return cameraOf(m_viewport).project(knobPoint()); }

bool TextGizmo::nearKnob(QPointF px) const {
    return knobVisible() && QLineF(knobOnScreen(), px).length() < kKnobGrabPx;
}

bool TextGizmo::frameAt(QPointF px, double &x, double &y) const {
    if(!m_frame) return false;
    QVector3D ro, rd;
    cameraOf(m_viewport).ray(px, ro, rd);
    const gp_Pnt o(ro.x(), ro.y(), ro.z());
    const gp_Dir d(rd.x(), rd.y(), rd.z());
    if(m_frame->planar) {
        const gp_Dir n = m_frame->plane.Direction();
        const double den = gp_Vec(d).Dot(gp_Vec(n));
        if(std::fabs(den) < 1e-9) return false;
        const double t = gp_Vec(o, m_frame->plane.Location()).Dot(gp_Vec(n)) / den;
        return m_frame->locate(o.Translated(gp_Vec(d) * t), x, y);
    }
    // The hit nearest the eye on the curved surface.
    GeomAPI_IntCS hits(new Geom_Line(o, d), m_frame->surface);
    if(!hits.IsDone() || hits.NbPoints() == 0) return false;
    int best = 0;
    double bestT = 1e300;
    for(int i = 1; i <= hits.NbPoints(); ++i) {
        const double t = gp_Vec(o, hits.Point(i)).Dot(gp_Vec(d));
        if(t > 0 && t < bestT) {
            bestT = t;
            best = i;
        }
    }
    return best && m_frame->locate(hits.Point(best), x, y);
}

bool TextGizmo::mousePress(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton) return false;
    if(nearKnob(e->position())) {
        double x, y;
        if(!frameAt(e->position(), x, y)) return false;
        m_grabX = m_x - x;
        m_grabY = m_y - y;
        m_moving = true;
        m_viewport->setCursor(cursor());
        return true;
    }
    return m_plane.mousePress(e);
}

bool TextGizmo::mouseMove(QMouseEvent *e) {
    if(m_moving) {
        double x, y;
        if(frameAt(e->position(), x, y)) {
            const double step = (e->modifiers() & Qt::ShiftModifier) ? 1.0 : 0.1;
            const double nx = roundTo(x + m_grabX, step), ny = roundTo(y + m_grabY, step);
            if(std::fabs(nx - m_x) > 1e-12 || std::fabs(ny - m_y) > 1e-12) {
                m_x = nx;
                m_y = ny;
                if(onMove) onMove(nx, ny);
            }
        }
        return true;
    }
    const bool hot = nearKnob(e->position());
    if(hot != m_hot) {
        m_hot = hot;
        m_viewport->setCursor(cursor());
        m_viewport->refreshOverlay();
        if(hot) return true;
    }
    if(hot) return true;
    return m_plane.mouseMove(e);
}

bool TextGizmo::mouseRelease(QMouseEvent *e) {
    if(m_moving && e->button() == Qt::LeftButton) {
        m_moving = false;
        m_viewport->setCursor(cursor());
        if(onRelease) onRelease();
        return true;
    }
    return m_plane.mouseRelease(e);
}

Qt::CursorShape TextGizmo::cursor() const {
    if(m_moving) return Qt::ClosedHandCursor;
    if(m_hot) return Qt::SizeAllCursor;
    return m_plane.cursor();
}

void TextGizmo::contribute(RenderScene &scene) {
    if(!m_outlines.empty()) {
        LineBatch b;
        b.color = kOutline;
        b.width = 1.8f;
        for(const auto &loop : m_outlines)
            for(size_t i = 0; i < loop.size(); ++i) {
                b.segments.push_back(loop[i]);
                b.segments.push_back(loop[(i + 1) % loop.size()]);
            }
        scene.lines.push_back(std::move(b));
    }
    if(m_visible) m_plane.contribute(scene);
}

void TextGizmo::paintOverlay(QPainter &p) {
    if(!m_visible) return;
    m_plane.paintOverlay(p);
    if(!knobVisible()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF k = knobOnScreen();
    const bool hot = m_hot || m_moving;
    const double r = hot ? kKnobPx + 1.5 : kKnobPx;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 45));
    p.drawEllipse(k + QPointF(0, 1.2), r + 1.0, r + 1.0);
    p.setPen(QPen(hot ? QColor(255, 255, 255) : kInput, 2.0));
    p.setBrush(hot ? kInput : QColor(255, 255, 255));
    p.drawEllipse(k, r, r);
    // A small four-way arrow.
    const QColor ink = hot ? QColor(255, 255, 255) : kInput;
    p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const double a = r - 3.0, h = 2.0;
    p.drawLine(k + QPointF(-a, 0), k + QPointF(a, 0));
    p.drawLine(k + QPointF(0, -a), k + QPointF(0, a));
    for(const QPointF &d : {QPointF(1, 0), QPointF(-1, 0), QPointF(0, 1), QPointF(0, -1)}) {
        const QPointF tip = k + d * a, side(-d.y(), d.x());
        p.drawLine(tip, tip - d * h + side * h);
        p.drawLine(tip, tip - d * h - side * h);
    }
    p.restore();
}

// --- the command --------------------------------------------------------------------------

TextCommand::TextCommand(const CommandContext &ctx, cad::FeatureId editing)
    : Command(ctx, editing), m_gizmo(ctx.viewport) {}

IconId TextCommand::iconId() const { return IconId::Emboss; }

void TextCommand::setup() {
    cad::Document &doc = *m_ctx.doc;
    CommandPanel &panel = *m_ctx.panel;
    if(isEditing()) m_original = std::dynamic_pointer_cast<const cad::TextFeature>(doc.feature(m_editing));
    const cad::TextFeature *o = m_original.get();

    m_faceField = panel.addSelection(tr("Face"), tr("Click a face"), "textFace");
    m_text = panel.addTextBox(tr("Text"), "textText");
    QStringList fonts;
    for(const std::string &f : cad::availableFonts()) fonts << QString::fromStdString(f);
    const QString font = QString::fromStdString(o ? o->font : std::string("DejaVu Sans"));
    if(!fonts.contains(font)) fonts.prepend(font);
    m_font = panel.addChoice(tr("Font"), fonts, "textFont");
    m_font->setCurrentText(font);
    m_font->setMaxVisibleItems(16);
    m_bold = panel.addCheck(tr("Bold"), "textBold");
    m_italic = panel.addCheck(tr("Italic"), "textItalic");
    m_sizeField = panel.addValue(tr("Size"), cad::ValueKind::Length, evaluator(), "textSize");
    m_letterSpacingField = panel.addValue(tr("Letter Spacing"), cad::ValueKind::Length, evaluator(), "textLetterSpacing");
    m_lineSpacingField = panel.addValue(tr("Line Spacing"), cad::ValueKind::Scalar, evaluator(), "textLineSpacing");
    m_lineSpacingField->setToolTip(tr("The distance from one line to the next, as a multiple of the size."));
    m_xField = panel.addValue(tr("X"), cad::ValueKind::Length, evaluator(), "textX");
    m_yField = panel.addValue(tr("Y"), cad::ValueKind::Length, evaluator(), "textY");
    m_xField->setToolTip(tr("Where the middle of the text is on the face. On a flat face: along the face from the "
                            "origin (on a top face, world X and Y). On a curved face: along the surface from the "
                            "middle of the face."));
    m_yField->setToolTip(m_xField->toolTip());
    m_rotationField = panel.addAngle(tr("Rotation"), evaluator(), "textRotation", kRotation);
    m_direction = panel.addChoice(tr("Type"), {tr("Engrave (cut in)"), tr("Emboss (raised)")}, "textDirection");
    m_depthField = panel.addValue(tr("Depth"), cad::ValueKind::Length, evaluator(), "textDepth");
    m_reverse = panel.addCheck(tr("Reverse"), "textReverse");
    m_reverse->setToolTip(tr("Mirrors the letters, so they read the right way from the other side (stamps, "
                             "moulds, text printed face down)."));

    auto slot = [&](const cad::ParamSlot *s, const char *fallback) {
        return s && !s->empty() ? *s : doc.makeSlot(fallback);
    };
    m_size = slot(o ? &o->size : nullptr, "5 mm");
    m_letterSpacing = slot(o ? &o->letterSpacing : nullptr, "0 mm");
    m_lineSpacing = slot(o ? &o->lineSpacing : nullptr, "1.2");
    m_x = slot(o ? &o->x : nullptr, "0 mm");
    m_y = slot(o ? &o->y : nullptr, "0 mm");
    m_rotation = slot(o ? &o->rotation : nullptr, "0 deg");
    m_depth = slot(o ? &o->depth : nullptr, "0.6 mm");
    const std::pair<ValueField *, const cad::ParamSlot *> values[] = {
        {m_sizeField, &m_size}, {m_letterSpacingField, &m_letterSpacing}, {m_lineSpacingField, &m_lineSpacing},
        {m_xField, &m_x},       {m_yField, &m_y},                         {m_rotationField, &m_rotation},
        {m_depthField, &m_depth}};
    for(const auto &[field, s] : values) {
        field->setExpression(QString::fromStdString(s->expr));
        connect(field, &ValueField::edited, this, [this] {
            updateCanvas();
            emit inputsChanged();
        });
    }
    if(o) {
        if(!o->face.empty()) m_face = InputRef::ofTopo(o->face);
        m_text->setPlainText(QString::fromStdString(o->text));
        m_bold->setChecked(o->bold);
        m_italic->setChecked(o->italic);
        m_reverse->setChecked(o->mirror);
        m_direction->setCurrentIndex(o->direction == cad::TextDirection::Emboss ? 1 : 0);
    } else {
        m_text->setPlainText(tr("Text"));
    }
    m_text->selectAll();
    auto changed = [this] {
        updateCanvas();
        emit inputsChanged();
    };
    connect(m_text, &QPlainTextEdit::textChanged, this, changed);
    connect(m_font, &QComboBox::currentTextChanged, this, changed);
    for(QCheckBox *c : {m_bold, m_italic, m_reverse}) connect(c, &QCheckBox::toggled, this, changed);
    connect(m_direction, &QComboBox::currentIndexChanged, this, [this, changed] {
        m_ctx.panel->setRowLabel(m_depthField, m_direction->currentIndex() == 1 ? tr("Height") : tr("Depth"));
        changed();
    });
    connect(m_faceField, &SelectionField::cleared, this, [this] {
        m_face.reset();
        updateMarks();
        updateCanvas();
        emit inputsChanged();
    });

    // The canvas handles type into the panel.
    m_gizmo.onMove = [this](double x, double y) {
        m_xField->setExpression(lengthText(x));
        m_yField->setExpression(lengthText(y));
        updateCanvas();
        emit inputsChanged();
    };
    m_gizmo.plane().onRingDrag = [this](int, double a) {
        m_rotationField->setExpression(QString::fromStdString(SketchEditor::formatExpression(a, cad::ValueKind::Angle)));
        updateCanvas();
        emit inputsChanged();
    };
    m_gizmo.plane().arrow().onDrag = [this](double d) {
        m_depthField->setExpression(lengthText(std::max(0.05, roundTo(std::fabs(d), 0.05))));
        updateCanvas();
        emit inputsChanged();
    };
    m_gizmo.plane().onFocusChanged = [this] { m_ctx.viewport->refreshOverlay(); };

    SelectFilter sf;
    sf.edges = sf.vertices = sf.bodies = sf.profiles = false;
    m_ctx.view->setFilter(sf);
    m_faceField->setActive(true);
    updateMarks();
    updateCanvas();
    if(m_face) {
        m_text->setFocus(Qt::OtherFocusReason);
        m_text->selectAll();
    }
}

void TextCommand::updateMarks() {
    ModelView::InputMarks marks;
    // The face is marked until the letters show on it (then they would be hidden under it).
    if(const cad::StatePtr base = baseState())
        if(m_face && !m_laid.ok) markInput(*m_ctx.view, *base, *m_face, -1, kInput, marks);
    m_ctx.view->setInputMarks(std::move(marks));
    m_faceField->setCount(m_face ? 1 : 0);
}

std::optional<TopoDS_Face> TextCommand::faceShape() const {
    const cad::StatePtr base = baseState();
    if(!base || !m_face) return std::nullopt;
    const cad::ResolvedRef r = cad::resolveRef(*base, m_face->topo);
    if(!r.ok || r.shape.ShapeType() != TopAbs_FACE) return std::nullopt;
    return TopoDS::Face(r.shape);
}

bool TextCommand::placement(cad::TextPlacementInput &in) const {
    in.text = m_text->toPlainText().toStdString();
    in.style.font = m_font->currentText().toStdString();
    in.style.bold = m_bold->isChecked();
    in.style.italic = m_italic->isChecked();
    in.style.mirror = m_reverse->isChecked();
    in.style.size = m_sizeField->value().value_or(0.0);
    in.style.letterSpacing = m_letterSpacingField->value().value_or(0.0);
    in.style.lineSpacing = m_lineSpacingField->value().value_or(1.2);
    in.x = m_xField->value().value_or(0.0);
    in.y = m_yField->value().value_or(0.0);
    in.rotation = m_rotationField->value().value_or(0.0);
    return in.style.size > 0.0 && in.text.find_first_not_of(" \t\r\n") != std::string::npos;
}

void TextCommand::updateCanvas() {
    const std::optional<TopoDS_Face> face = faceShape();
    cad::TextPlacementInput in;
    const bool ready = placement(in);
    m_laid = cad::TextOnFace();
    std::optional<cad::FaceTextFrame> frame;
    if(face) {
        std::string err;
        cad::FaceTextFrame f;
        if(cad::faceTextFrame(*face, f, err)) frame = f;
        if(ready) m_laid = cad::layTextOnFace(*face, in);
    }
    m_gizmo.setFrame(frame);
    m_gizmo.setVisible(frame.has_value());
    m_gizmo.setKnob(in.x, in.y);
    const bool emboss = m_direction->currentIndex() == 1;
    const double depth = std::max(0.0, m_depthField->value().value_or(0.0));
    std::vector<std::vector<QVector3D>> outlines;
    if(m_laid.ok) {
        // On the face, or on top of raised letters, just clear of the surface.
        const double lift = (emboss ? depth : 0.0) + 0.02;
        for(const auto &loop : m_laid.outlinesXY) {
            std::vector<QVector3D> pts;
            pts.reserve(loop.size());
            for(const auto &[x, y] : loop) pts.push_back(toQ(m_laid.frame.point(x, y, lift).XYZ()));
            outlines.push_back(std::move(pts));
        }
    }
    m_gizmo.setOutlines(std::move(outlines));
    if(m_laid.ok != m_marked) {
        m_marked = m_laid.ok;
        updateMarks();
    }
    PlaneGizmo &g = m_gizmo.plane();
    PlaneGizmo::Ring &ring = g.ring(0);
    g.ring(1).visible = false;
    ring.visible = frame.has_value();
    g.arrow().setVisible(frame.has_value());
    if(frame) {
        const gp_Pnt at = frame->point(in.x, in.y);
        const gp_Dir n = frame->normal(in.x, in.y);
        gp_Dir xDir, yDir;
        frame->axes(in.x, in.y, xDir, yDir);
        ring.center = toQ(at.XYZ());
        ring.axis = toQ(n.XYZ());
        ring.zero = toQ(xDir.XYZ());
        ring.angle = in.rotation;
        g.arrow().setAxis(toQ(at.XYZ()), toQ((emboss ? n : n.Reversed()).XYZ()));
        g.arrow().setDistance(depth);
    }
    if(g.focus() != PlaneGizmo::Part::Ring0 && g.focus() != PlaneGizmo::Part::Arrow) g.setFocus(PlaneGizmo::Part::Arrow);
    m_ctx.viewport->refreshOverlay();
}

void TextCommand::previewed(const cad::StatePtr &) {
    updateMarks();
    updateCanvas();
}

ValueField *TextCommand::canvasValue() const {
    return m_gizmo.plane().focus() == PlaneGizmo::Part::Ring0 ? m_rotationField : m_depthField;
}

std::optional<QVector3D> TextCommand::canvasAnchor() const {
    if(!m_gizmo.knobVisible()) return std::nullopt;
    if(m_gizmo.plane().focus() == PlaneGizmo::Part::Ring0) return m_gizmo.plane().knobPoint(0);
    return m_gizmo.plane().arrow().headPoint();
}

QColor TextCommand::canvasAccent() const {
    return m_gizmo.plane().focus() == PlaneGizmo::Part::Ring0 ? kRotation : QColor();
}

void TextCommand::picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers) {
    if(!item || item->kind != SelectionItem::Kind::Face || !hit.valid()) return;
    const std::optional<InputRef> r = inputRefOf(*m_ctx.view, *item);
    if(!r || r->createdBy(isEditing() ? m_editing : m_ctx.doc->nextFeatureId())) return;
    const cad::StatePtr st = m_ctx.view->state();
    const cad::Body *b = st ? st->body(item->body) : nullptr;
    if(!b) return;
    cad::FaceTextFrame frame;
    std::string err;
    if(!cad::faceTextFrame(b->shape.face(item->index), frame, err)) {
        m_ctx.panel->setMessage(QString::fromStdString(err), cad::Severity::Error);
        return;
    }
    double x = 0, y = 0;
    if(!frame.locate(gp_Pnt(hit.point.x(), hit.point.y(), hit.point.z()), x, y)) return;
    // The text goes where the face was clicked.
    const bool first = !m_face;
    m_face = *r;
    m_xField->setExpression(lengthText(roundTo(x, 0.1)));
    m_yField->setExpression(lengthText(roundTo(y, 0.1)));
    updateMarks();
    updateCanvas();
    if(first) {
        m_text->setFocus(Qt::OtherFocusReason);
        m_text->selectAll();
    }
    emit inputsChanged();
}

std::shared_ptr<cad::Feature> TextCommand::build(QString &why) {
    if(!m_face) {
        why = tr("Click the face the text goes on.");
        return nullptr;
    }
    const std::string text = m_text->toPlainText().toStdString();
    if(text.find_first_not_of(" \t\r\n") == std::string::npos) {
        why = tr("Type the text.");
        return nullptr;
    }
    const std::pair<ValueField *, QString> fields[] = {
        {m_sizeField, tr("Size")},   {m_letterSpacingField, tr("Letter spacing")}, {m_lineSpacingField, tr("Line spacing")},
        {m_xField, tr("X")},         {m_yField, tr("Y")},                          {m_rotationField, tr("Rotation")},
        {m_depthField, tr("Depth")}};
    for(const auto &[f, what] : fields)
        if(!f->valid()) {
            why = tr("%1: enter a value").arg(what);
            return nullptr;
        }
    auto f = m_original ? std::static_pointer_cast<cad::TextFeature>(m_original->clone())
                        : std::make_shared<cad::TextFeature>();
    f->face = m_face->topo;
    f->text = text;
    f->font = m_font->currentText().toStdString();
    f->bold = m_bold->isChecked();
    f->italic = m_italic->isChecked();
    f->mirror = m_reverse->isChecked();
    f->direction = m_direction->currentIndex() == 1 ? cad::TextDirection::Emboss : cad::TextDirection::Engrave;
    auto take = [](const cad::ParamSlot &s, ValueField *v) { return cad::ParamSlot{s.name, v->expression().toStdString()}; };
    f->size = take(m_size, m_sizeField);
    f->letterSpacing = take(m_letterSpacing, m_letterSpacingField);
    f->lineSpacing = take(m_lineSpacing, m_lineSpacingField);
    f->x = take(m_x, m_xField);
    f->y = take(m_y, m_yField);
    f->rotation = take(m_rotation, m_rotationField);
    f->depth = take(m_depth, m_depthField);
    return f;
}

} // namespace cadjitsu
