#include "sketch/SketchTools.h"

#include "command/CommandPanel.h"
#include "sketch/HeadsUpInput.h"
#include "sketch/SketchMode.h"
#include "sketch/SketchOffset.h"
#include "sketch/SketchPalette.h"
#include "sketch/SketchText.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QIcon>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>

#include <algorithm>
#include <cmath>

namespace cadjitsu {

using cad::SkCon;
using cad::SkEntity;
using cad::SkType;
using cad::Vec2;
using HitKind = SketchHit::Kind;

namespace {

const QColor kPickColor(58, 163, 255);
const double kInferTolerance = 3.0 * cad::kPi / 180.0;

double pxDist(QPointF a, QPointF b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); }

// Snaps that attach the new point to existing geometry (no H/V inference then).
bool strongSnap(const SketchSnap &s) { return s.attaches(); }

QPointF unit(QPointF v) {
    const double l = std::hypot(v.x(), v.y());
    return l > 1e-9 ? v / l : QPointF(1, 0);
}

// Glyph drawn next to the cursor for an inferred or snapped relation.
void paintHint(QPainter &p, QPointF at, IconId id) {
    paintGlyphChip(p, QRectF(at.x() + 10, at.y() + 10, 18, 18), icon(id), QColor(160, 170, 184));
}

std::optional<IconId> snapHint(const SketchSnap &s) {
    switch(s.kind) {
    case SketchSnap::Kind::Point:
    case SketchSnap::Kind::Origin: return IconId::Coincident;
    case SketchSnap::Kind::Midpoint: return IconId::Midpoint;
    case SketchSnap::Kind::Quadrant:
    case SketchSnap::Kind::OnCurve: return IconId::Coincident;
    default: return std::nullopt;
    }
}

} // namespace

QString sketchToolName(SketchToolKind k) {
    switch(k) {
    case SketchToolKind::Select: return SketchTool::tr("Select");
    case SketchToolKind::Line: return SketchTool::tr("Line");
    case SketchToolKind::Rectangle: return SketchTool::tr("2-Point Rectangle");
    case SketchToolKind::CenterRectangle: return SketchTool::tr("Center Rectangle");
    case SketchToolKind::Circle: return SketchTool::tr("Center Diameter Circle");
    case SketchToolKind::Arc: return SketchTool::tr("3-Point Arc");
    case SketchToolKind::Point: return SketchTool::tr("Point");
    case SketchToolKind::Dimension: return SketchTool::tr("Sketch Dimension");
    case SketchToolKind::Coincident: return SketchTool::tr("Coincident");
    case SketchToolKind::HorizontalVertical: return SketchTool::tr("Horizontal/Vertical");
    case SketchToolKind::Parallel: return SketchTool::tr("Parallel");
    case SketchToolKind::Perpendicular: return SketchTool::tr("Perpendicular");
    case SketchToolKind::Tangent: return SketchTool::tr("Tangent");
    case SketchToolKind::Equal: return SketchTool::tr("Equal");
    case SketchToolKind::Midpoint: return SketchTool::tr("Midpoint");
    case SketchToolKind::Concentric: return SketchTool::tr("Concentric");
    case SketchToolKind::Fix: return SketchTool::tr("Fix/Unfix");
    case SketchToolKind::Symmetric: return SketchTool::tr("Symmetric");
    case SketchToolKind::Move: return SketchTool::tr("Move / Copy");
    case SketchToolKind::Offset: return SketchTool::tr("Offset");
    case SketchToolKind::Text: return SketchTool::tr("Text");
    }
    return {};
}

// ---------------------------------------------------------------------------

SketchEditor &SketchTool::editor() const { return *m_mode.editor(); }
HeadsUpInput &SketchTool::hud() const { return m_mode.hud(); }

void SketchTool::contribute(RenderScene &scene) {
    editor().contribute(scene);
    contributeTool(scene);
}

void SketchTool::paintOverlay(QPainter &p) {
    editor().paintOverlay(p);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    paintToolOverlay(p);
    p.restore();
}

void SketchTool::setHover(QPointF px, unsigned filter) {
    const SketchHit h = editor().hitTest(px, filter);
    if(!(h == editor().hover)) {
        editor().hover = h;
        editor().refreshView();
    }
}

bool SketchTool::isValueKey(const QKeyEvent *e) {
    if(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)) return false;
    const QString t = e->text();
    if(t.isEmpty()) return false;
    const QChar c = t.at(0);
    return c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char('(');
}

bool SketchTool::commonKey(QKeyEvent *e) {
    switch(e->key()) {
    case Qt::Key_Escape:
        if(!cancel()) {
            if(kind() != SketchToolKind::Select) m_mode.setTool(SketchToolKind::Select);
            else editor().clearSelection();
        }
        return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        m_mode.deleteSelection();
        return true;
    default:
        break;
    }
    if(isValueKey(e) && hud().visible()) {
        hud().beginTyping(e->text());
        return true;
    }
    return false;
}

void SketchTool::highlight(RenderScene &scene, const std::vector<SketchHit> &hits, const QColor &color) const {
    LineBatch lb;
    lb.color = color;
    lb.width = 3.0f;
    PointBatch pb;
    pb.color = color;
    pb.outline = color.darker(140);
    pb.size = 10.0f;
    const cad::Sketch &s = editor().sketch();
    const double ext = std::max(10.0, double(editor().viewport()->content().gridExtent));
    for(const SketchHit &h : hits) {
        switch(h.kind) {
        case HitKind::Point: pb.points.push_back(editor().toWorld(s.pointPos(h.id))); break;
        case HitKind::Origin: pb.points.push_back(editor().toWorld({0, 0})); break;
        case HitKind::Curve:
            if(const SkEntity *e = s.find(h.id))
                for(const auto &pl : editor().strokes(*e))
                    for(size_t k = 0; k + 1 < pl.size(); ++k) {
                        lb.segments.push_back(editor().toWorld(pl[k]));
                        lb.segments.push_back(editor().toWorld(pl[k + 1]));
                    }
            break;
        case HitKind::Axis: {
            const Vec2 d = h.id == cad::kSketchXAxis ? Vec2(1, 0) : Vec2(0, 1);
            lb.segments.push_back(editor().toWorld(d * -ext));
            lb.segments.push_back(editor().toWorld(d * ext));
            break;
        }
        default: break;
        }
    }
    if(!lb.segments.empty()) scene.lines.push_back(lb);
    if(!pb.points.empty()) scene.points.push_back(pb);
}

namespace {

// ---------------------------------------------------------------------------
// Select: pick, box-select and drag sketch geometry and dimension labels.

class SelectTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Select; }
    QString prompt() const override {
        return tr("Select or drag sketch geometry. Double-click a line, circle, arc or dimension to type its size.");
    }
    Qt::CursorShape cursor() const override { return Qt::ArrowCursor; }

    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        m_press = e->position();
        m_pressed = true;
        m_moved = m_box = m_dragging = false;
        m_additive = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::MetaModifier);
        m_hit = editor().hitTest(m_press);
        m_wasSelected = editor().isSelected(m_hit);
        if(m_hit.valid() && m_hit.kind != HitKind::Profile && !m_additive && !m_wasSelected) editor().select(m_hit, false);
        m_canDrag = !m_additive &&
                    (m_hit.kind == HitKind::Point || m_hit.kind == HitKind::Curve || m_hit.kind == HitKind::Dimension);
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        const QPointF pos = e->position();
        if(!m_pressed) {
            setHover(pos, HitDefault);
            return true;
        }
        if(!m_moved) {
            if(pxDist(pos, m_press) < 4.0) return true;
            m_moved = true;
            if(m_canDrag) m_dragging = editor().beginDrag(m_hit, m_press);
            else m_box = true;
        }
        if(m_dragging) {
            editor().dragTo(pos);
        } else if(m_box) {
            m_rect = QRectF(m_press, pos).normalized();
            m_crossing = pos.x() < m_press.x();
            editor().refreshView();
        }
        return true;
    }
    bool mouseRelease(QMouseEvent *e) override {
        if(!m_pressed || e->button() != Qt::LeftButton) return false;
        m_pressed = false;
        if(m_dragging) {
            editor().endDrag();
            m_dragging = false;
        } else if(m_box) {
            m_box = false;
            editor().selectEntities(editor().entitiesInRect(m_rect, m_crossing), m_additive);
        } else if(m_hit.valid()) {
            if(m_additive) editor().select(m_hit, true);
            else if(m_wasSelected || m_hit.kind == HitKind::Profile) editor().select(m_hit, false);
        } else if(!m_additive) {
            editor().clearSelection();
        }
        return true;
    }
    bool mouseDoubleClick(QMouseEvent *e) override {
        const SketchHit hit = editor().hitTest(e->position());
        if(hit.kind == HitKind::Dimension) m_mode.editDimension(hit.id);
        else if(hit.kind == HitKind::Curve && (e->modifiers() & Qt::ShiftModifier))
            editor().selectEntities(editor().connectedChain(hit.id), false);
        else if(hit.kind == HitKind::Curve) {
            const SkEntity *ent = editor().sketch().find(hit.id);
            if(ent && ent->isText()) m_mode.editText(hit.id);
            else m_mode.editSize(hit.id);
        }
        return true;
    }
    bool keyPress(QKeyEvent *e) override { return commonKey(e); }
    bool cancel() override {
        if(!m_dragging) return false;
        editor().cancelDrag();
        m_dragging = m_pressed = false;
        return true;
    }

protected:
    void paintToolOverlay(QPainter &p) override {
        if(!m_box) return;
        p.setPen(QPen(m_crossing ? QColor(40, 150, 70) : QColor(40, 110, 210), 1.2,
                      m_crossing ? Qt::DashLine : Qt::SolidLine));
        p.setBrush(m_crossing ? QColor(60, 180, 90, 35) : QColor(60, 130, 220, 35));
        p.drawRect(m_rect);
    }

private:
    QPointF m_press;
    bool m_pressed = false, m_moved = false, m_box = false, m_dragging = false, m_additive = false;
    bool m_canDrag = false, m_wasSelected = false, m_crossing = false;
    SketchHit m_hit;
    QRectF m_rect;
};

// ---------------------------------------------------------------------------
// Line: click-click chains of lines, with horizontal / vertical inference and
// heads-up length and angle.

class LineTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Line; }
    QString prompt() const override {
        return m_hasStart ? tr("Click the next point, or type a length (Tab for the angle). Esc or double-click ends the line.")
                          : tr("Click the start point of the line.");
    }
    void activate() override {
        hud().setFields({{tr("Length"), cad::ValueKind::Length}, {tr("Angle"), cad::ValueKind::Angle}});
    }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        update(e->position());
        if(!m_hasStart) start();
        else commitSegment();
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        update(e->position());
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool mouseDoubleClick(QMouseEvent *) override {
        endChain();
        return true;
    }
    bool keyPress(QKeyEvent *e) override {
        if((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_hasStart) {
            commitSegment();
            return true;
        }
        return commonKey(e);
    }
    bool cancel() override {
        if(!m_hasStart) return false;
        endChain();
        return true;
    }
    void hudCommit() override {
        if(m_hasStart) commitSegment();
    }
    void hudChanged() override { update(m_cursor); }

protected:
    void paintToolOverlay(QPainter &p) override {
        if(!m_hasStart) {
            if(auto id = snapHint(m_snap)) paintHint(p, m_cursor, *id);
            return;
        }
        if(m_inferH || m_inferV) {
            const QPointF mid = (editor().toScreen(startPos()) + editor().toScreen(m_end)) / 2;
            paintHint(p, mid, m_inferH ? IconId::Horizontal : IconId::Vertical);
        } else if(m_endOnSnap) {
            if(auto id = snapHint(m_snap)) paintHint(p, m_cursor, *id);
        }
    }

private:
    Vec2 startPos() const { return m_startPoint ? editor().posOf(m_startPoint) : m_startSnap.pos; }

    void start() {
        m_startSnap = m_snap;
        m_startPoint = m_snap.kind == SketchSnap::Kind::Point ? m_snap.entity : 0;
        m_firstPoint = m_startPoint;
        m_hasStart = true;
        hud().unlockAll();
        update(m_cursor);
        m_mode.showStatus(prompt());
    }

    void endChain() {
        m_hasStart = false;
        m_startPoint = m_firstPoint = 0;
        hud().hide();
        hud().unlockAll();
        editor().clearPreview();
        m_mode.showStatus(prompt());
    }

    void update(QPointF px) {
        m_cursor = px;
        SketchEditor &ed = editor();
        m_snap = ed.snap(px);
        ed.previewLines.clear();
        if(!m_hasStart) {
            ed.previewSnap = m_snap;
            ed.refreshView();
            return;
        }
        const Vec2 s = startPos();
        Vec2 d = m_snap.pos - s;
        m_inferH = m_inferV = m_endOnSnap = false;
        const auto len = hud().value(0), ang = hud().value(1);
        auto infer = [&](double a) {
            if(std::fabs(std::sin(a)) < std::sin(kInferTolerance)) {
                m_inferH = true;
                return std::cos(a) >= 0 ? 0.0 : cad::kPi;
            }
            if(std::fabs(std::cos(a)) < std::sin(kInferTolerance)) {
                m_inferV = true;
                return std::sin(a) >= 0 ? cad::kPi / 2 : -cad::kPi / 2;
            }
            return a;
        };
        if(len || ang) {
            const double a = ang ? *ang : infer(std::atan2(d.y, d.x));
            const double l = len ? *len : d.length();
            d = Vec2(std::cos(a), std::sin(a)) * l;
        } else if(strongSnap(m_snap)) {
            m_endOnSnap = true;
            // Level or plumb with the snapped point (to within half a degree): keep
            // that as a constraint too. Any that over-constrain are dropped on commit.
            if(d.length() > 1e-9) {
                const double tight = std::sin(0.5 * cad::kPi / 180.0);
                if(std::fabs(d.y) < tight * d.length()) m_inferH = true;
                else if(std::fabs(d.x) < tight * d.length()) m_inferV = true;
            }
        } else if(d.length() > 1e-9) {
            const double a = infer(std::atan2(d.y, d.x));
            if(m_inferH || m_inferV) d = Vec2(std::cos(a), std::sin(a)) * (m_inferH ? std::fabs(d.x) : std::fabs(d.y));
        }
        m_end = s + d;
        ed.previewLines.push_back({s, m_end});
        ed.previewSnap = m_endOnSnap ? std::optional<SketchSnap>(m_snap) : std::nullopt;

        double a = std::atan2(d.y, d.x);
        if(a < 0) a += 2 * cad::kPi;
        hud().setLive(0, d.length());
        hud().setLive(1, a);
        const QPointF ss = ed.toScreen(s), se = ed.toScreen(m_end);
        const QPointF dir = unit(se - ss);
        QPointF n(-dir.y(), dir.x());
        if(n.y() > 0) n = -n;
        hud().place(0, (ss + se) / 2 + n * 26.0);
        hud().place(1, ss - n * 26.0 - dir * 30.0);
        ed.refreshView();
    }

    void commitSegment() {
        SketchEditor &ed = editor();
        const Vec2 s = startPos();
        if(distance(s, m_end) < 1e-6) return;
        cad::Sketch work = ed.sketch();
        std::vector<PendingConstraint> pending;
        int a = m_startPoint;
        if(!a || !work.find(a)) a = ed.pointForSnap(work, m_startSnap, pending);
        SketchSnap endSnap;
        if(m_endOnSnap) endSnap = m_snap;
        endSnap.pos = m_endOnSnap ? m_snap.pos : m_end;
        const int b = ed.pointForSnap(work, endSnap, pending);
        if(a == b) return;
        const int line = work.addLine(a, b);
        if(m_inferH) pending.push_back({SkCon::Horizontal, line});
        if(m_inferV) pending.push_back({SkCon::Vertical, line});
        const Vec2 d = m_end - s, mid = (s + m_end) * 0.5;
        const double off = ed.sketchUnitsPerPixel(mid) * 24.0;
        if(hud().value(0)) {
            PendingConstraint pc{SkCon::Distance, line};
            pc.expr = hud().expression(0).toStdString();
            pc.label = d.normalized().perp() * off;
            pending.push_back(pc);
        }
        if(const auto typed = hud().value(1); typed && !m_inferH && !m_inferV) {
            const double A = cad::normAngle(*typed);
            const double tol = 1e-9;
            if(std::fabs(std::sin(A)) < tol) {
                pending.push_back({SkCon::Horizontal, line});
            } else if(std::fabs(std::cos(A)) < tol) {
                pending.push_back({SkCon::Vertical, line});
            } else {
                // Angle to the sketch X axis; the label sits near the new line.
                PendingConstraint pc{SkCon::Angle, line, cad::kSketchXAxis};
                const double theta = A <= cad::kPi ? A : 2 * cad::kPi - A;
                pc.expr = theta == A ? hud().expression(1).toStdString()
                                     : SketchEditor::formatExpression(theta, cad::ValueKind::Angle);
                const Vec2 u = d.normalized();
                const Vec2 I = std::fabs(u.y) > 1e-12 ? s - u * (s.y / u.y) : s;
                Vec2 bis = (u + Vec2(1, 0)).normalized();
                if(bis.dot(mid - I) < 0) bis = -bis;
                pc.label = bis * std::max(distance(mid, I), off * 2);
                pending.push_back(pc);
            }
        }
        const bool closes = m_firstPoint != 0 && b == m_firstPoint;
        if(!ed.commit(tr("Line"), std::move(work), pending)) {
            endChain();
            return;
        }
        if(!m_firstPoint) m_firstPoint = a;
        m_startPoint = b;
        hud().unlockAll();
        if(closes) {
            endChain();
            return;
        }
        update(m_cursor);
    }

    QPointF m_cursor;
    SketchSnap m_snap, m_startSnap;
    int m_startPoint = 0, m_firstPoint = 0;
    bool m_hasStart = false;
    Vec2 m_end;
    bool m_endOnSnap = false, m_inferH = false, m_inferV = false;
};

// ---------------------------------------------------------------------------
// Rectangle: two corners, or centre and corner. Width and height can be typed.

class RectangleTool final : public SketchTool {
public:
    RectangleTool(SketchMode &mode, bool center) : SketchTool(mode), m_centerMode(center) {}
    SketchToolKind kind() const override {
        return m_centerMode ? SketchToolKind::CenterRectangle : SketchToolKind::Rectangle;
    }
    QString prompt() const override {
        if(!m_hasFirst)
            return m_centerMode ? tr("Click the centre of the rectangle.") : tr("Click the first corner of the rectangle.");
        return tr("Click the opposite corner, or type the width (Tab for the height) and press Enter.");
    }
    void activate() override {
        hud().setFields({{tr("Width"), cad::ValueKind::Length}, {tr("Height"), cad::ValueKind::Length}});
    }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        update(e->position());
        if(!m_hasFirst) {
            m_first = m_snap;
            m_hasFirst = true;
            hud().unlockAll();
            update(m_cursor);
            m_mode.showStatus(prompt());
        } else {
            commit();
        }
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        update(e->position());
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override {
        if((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_hasFirst) {
            commit();
            return true;
        }
        return commonKey(e);
    }
    bool cancel() override {
        if(!m_hasFirst) return false;
        reset();
        return true;
    }
    void hudCommit() override { commit(); }
    void hudChanged() override { update(m_cursor); }

protected:
    void paintToolOverlay(QPainter &p) override {
        if(auto id = snapHint(m_snap); id && (!m_hasFirst || m_endOnSnap)) paintHint(p, m_cursor, *id);
    }

private:
    void reset() {
        m_hasFirst = false;
        hud().hide();
        hud().unlockAll();
        editor().clearPreview();
        m_mode.showStatus(prompt());
    }

    void update(QPointF px) {
        m_cursor = px;
        SketchEditor &ed = editor();
        m_snap = ed.snap(px);
        ed.previewLines.clear();
        ed.previewConstruction.clear();
        if(!m_hasFirst) {
            ed.previewSnap = m_snap;
            ed.refreshView();
            return;
        }
        const Vec2 f = m_first.pos, q = m_snap.pos;
        Vec2 half = m_centerMode ? q - f : (q - f) * 0.5;
        const auto w = hud().value(0), h = hud().value(1);
        if(w) half.x = std::copysign(*w * 0.5, half.x == 0.0 ? 1.0 : half.x);
        if(h) half.y = std::copysign(*h * 0.5, half.y == 0.0 ? 1.0 : half.y);
        const Vec2 centre = m_centerMode ? f : f + half;
        m_a = centre - half;
        m_b = centre + half;
        m_endOnSnap = !w && !h && strongSnap(m_snap);
        const Vec2 c[4] = {m_a, {m_b.x, m_a.y}, m_b, {m_a.x, m_b.y}};
        for(int k = 0; k < 4; ++k) ed.previewLines.push_back({c[k], c[(k + 1) % 4]});
        if(m_centerMode) {
            ed.previewConstruction.push_back({c[0], c[2]});
            ed.previewConstruction.push_back({c[1], c[3]});
        }
        ed.previewSnap = m_endOnSnap ? std::optional<SketchSnap>(m_snap) : std::nullopt;
        hud().setLive(0, std::fabs(2 * half.x));
        hud().setLive(1, std::fabs(2 * half.y));
        const Vec2 lo(std::min(m_a.x, m_b.x), std::min(m_a.y, m_b.y)), hi(std::max(m_a.x, m_b.x), std::max(m_a.y, m_b.y));
        hud().place(0, ed.toScreen({(lo.x + hi.x) / 2, hi.y}) + QPointF(0, -22));
        hud().place(1, ed.toScreen({hi.x, (lo.y + hi.y) / 2}) + QPointF(52, 0));
        ed.refreshView();
    }

    void commit() {
        if(!m_hasFirst) return;
        SketchEditor &ed = editor();
        const Vec2 lo(std::min(m_a.x, m_b.x), std::min(m_a.y, m_b.y)), hi(std::max(m_a.x, m_b.x), std::max(m_a.y, m_b.y));
        if(hi.x - lo.x < 1e-6 || hi.y - lo.y < 1e-6) return;
        cad::Sketch work = ed.sketch();
        std::vector<PendingConstraint> pending;
        const Vec2 pos[4] = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
        int pts[4] = {0, 0, 0, 0};
        auto cornerOf = [&](Vec2 v) {
            for(int k = 0; k < 4; ++k)
                if(distance(pos[k], v) < 1e-9) return k;
            return -1;
        };
        if(!m_centerMode)
            if(const int k = cornerOf(m_a); k >= 0) pts[k] = ed.pointForSnap(work, m_first, pending);
        if(m_endOnSnap)
            if(const int k = cornerOf(m_b); k >= 0 && !pts[k]) pts[k] = ed.pointForSnap(work, m_snap, pending);
        for(int k = 0; k < 4; ++k)
            if(!pts[k]) pts[k] = work.addPoint(pos[k].x, pos[k].y);
        const int bottom = work.addLine(pts[0], pts[1]), right = work.addLine(pts[1], pts[2]);
        const int top = work.addLine(pts[2], pts[3]), left = work.addLine(pts[3], pts[0]);
        const std::vector<PendingConstraint> shape = {
            {SkCon::Horizontal, bottom}, {SkCon::Horizontal, top}, {SkCon::Vertical, right}, {SkCon::Vertical, left}};
        pending.insert(pending.begin(), shape.begin(), shape.end());
        if(m_centerMode) {
            const int c = ed.pointForSnap(work, m_first, pending);
            const int d1 = work.addLine(pts[0], pts[2], true);
            work.addLine(pts[1], pts[3], true);
            pending.push_back({SkCon::Midpoint, c, d1});
        }
        const double off = ed.sketchUnitsPerPixel((lo + hi) * 0.5) * 24.0;
        if(hud().value(0)) {
            PendingConstraint pc{SkCon::Distance, bottom};
            pc.expr = hud().expression(0).toStdString();
            pc.label = Vec2(0, -off);
            pending.push_back(pc);
        }
        if(hud().value(1)) {
            PendingConstraint pc{SkCon::Distance, left};
            pc.expr = hud().expression(1).toStdString();
            pc.label = Vec2(-off, 0);
            pending.push_back(pc);
        }
        ed.commit(m_centerMode ? tr("Center Rectangle") : tr("Rectangle"), std::move(work), pending);
        reset();
    }

    bool m_centerMode;
    bool m_hasFirst = false;
    SketchSnap m_first, m_snap;
    QPointF m_cursor;
    Vec2 m_a, m_b;
    bool m_endOnSnap = false;
};

// ---------------------------------------------------------------------------
// Circle: centre and diameter.

class CircleTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Circle; }
    QString prompt() const override {
        return m_hasCenter ? tr("Click to set the diameter, or type it and press Enter.") : tr("Click the centre of the circle.");
    }
    void activate() override { hud().setFields({{tr("Diameter"), cad::ValueKind::Length}}); }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        update(e->position());
        if(!m_hasCenter) {
            m_center = m_snap;
            m_hasCenter = true;
            hud().unlockAll();
            update(m_cursor);
            m_mode.showStatus(prompt());
        } else {
            commit();
        }
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        update(e->position());
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override {
        if((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_hasCenter) {
            commit();
            return true;
        }
        return commonKey(e);
    }
    bool cancel() override {
        if(!m_hasCenter) return false;
        reset();
        return true;
    }
    void hudCommit() override { commit(); }
    void hudChanged() override { update(m_cursor); }

protected:
    void paintToolOverlay(QPainter &p) override {
        if(auto id = snapHint(m_snap); id && (!m_hasCenter || m_endOnSnap)) paintHint(p, m_cursor, *id);
    }

private:
    void reset() {
        m_hasCenter = false;
        hud().hide();
        hud().unlockAll();
        editor().clearPreview();
        m_mode.showStatus(prompt());
    }

    void update(QPointF px) {
        m_cursor = px;
        SketchEditor &ed = editor();
        m_snap = ed.snap(px);
        ed.previewLines.clear();
        if(!m_hasCenter) {
            ed.previewSnap = m_snap;
            ed.refreshView();
            return;
        }
        const Vec2 c = m_center.pos;
        Vec2 u = m_snap.pos - c;
        m_radius = u.length();
        u = m_radius > 1e-12 ? u / m_radius : Vec2(1, 0);
        m_dir = u;
        const auto dia = hud().value(0);
        if(dia) m_radius = *dia * 0.5;
        m_endOnSnap = !dia && m_snap.kind == SketchSnap::Kind::Point;
        const int n = 96;
        for(int i = 0; i < n; ++i) {
            const double t0 = 2 * cad::kPi * i / n, t1 = 2 * cad::kPi * (i + 1) / n;
            ed.previewLines.push_back(
                {c + Vec2(std::cos(t0), std::sin(t0)) * m_radius, c + Vec2(std::cos(t1), std::sin(t1)) * m_radius});
        }
        ed.previewLines.push_back({c, c + u * m_radius});
        ed.previewSnap = m_endOnSnap ? std::optional<SketchSnap>(m_snap) : std::nullopt;
        hud().setLive(0, 2 * m_radius);
        const QPointF edge = ed.toScreen(c + u * m_radius), sc = ed.toScreen(c);
        hud().place(0, edge + unit(edge - sc) * 50.0);
        ed.refreshView();
    }

    void commit() {
        if(!m_hasCenter || m_radius < 1e-6) return;
        SketchEditor &ed = editor();
        cad::Sketch work = ed.sketch();
        std::vector<PendingConstraint> pending;
        const int centre = ed.pointForSnap(work, m_center, pending);
        const int circle = work.addCircle(centre, m_radius);
        if(hud().value(0)) {
            PendingConstraint pc{SkCon::Diameter, circle};
            pc.expr = hud().expression(0).toStdString();
            pc.label = m_dir * (m_radius + ed.sketchUnitsPerPixel(m_center.pos) * 30.0);
            pending.push_back(pc);
        } else if(m_endOnSnap) {
            pending.push_back({SkCon::PointOnCurve, m_snap.entity, circle});
        }
        ed.commit(tr("Circle"), std::move(work), pending);
        reset();
    }

    bool m_hasCenter = false;
    SketchSnap m_center, m_snap;
    QPointF m_cursor;
    double m_radius = 0.0;
    Vec2 m_dir{1, 0};
    bool m_endOnSnap = false;
};

// ---------------------------------------------------------------------------
// Arc through three points: start, end, then a point on the arc.

class ArcTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Arc; }
    QString prompt() const override {
        switch(m_state) {
        case 0: return tr("Click the start point of the arc.");
        case 1: return tr("Click the end point of the arc.");
        default: return tr("Click a point on the arc.");
        }
    }
    void activate() override { hud().setFields({}); }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        update(e->position());
        if(m_state == 0) {
            m_start = m_snap;
            m_state = 1;
        } else if(m_state == 1) {
            if(distance(m_snap.pos, m_start.pos) < 1e-6) return true;
            m_end = m_snap;
            m_state = 2;
        } else {
            commit();
        }
        update(m_cursor);
        m_mode.showStatus(prompt());
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        update(e->position());
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override { return commonKey(e); }
    bool cancel() override {
        if(m_state == 0) return false;
        m_state = 0;
        editor().clearPreview();
        m_mode.showStatus(prompt());
        return true;
    }

protected:
    void paintToolOverlay(QPainter &p) override {
        if(auto id = snapHint(m_snap)) paintHint(p, m_cursor, *id);
    }

private:
    // Circle through three points; false if they are collinear.
    static bool circumcircle(Vec2 a, Vec2 b, Vec2 c, Vec2 &centre) {
        const double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
        if(std::fabs(d) < 1e-12) return false;
        const double a2 = a.length2(), b2 = b.length2(), c2 = c.length2();
        centre = Vec2((a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d,
                      (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d);
        return true;
    }

    void update(QPointF px) {
        m_cursor = px;
        SketchEditor &ed = editor();
        m_snap = ed.snap(px);
        ed.previewLines.clear();
        ed.previewSnap = m_snap;
        if(m_state == 1) {
            ed.previewLines.push_back({m_start.pos, m_snap.pos});
        } else if(m_state == 2) {
            const Vec2 S = m_start.pos, E = m_end.pos, T = m_snap.pos;
            Vec2 C;
            if(!circumcircle(S, T, E, C)) {
                ed.previewLines.push_back({S, E});
            } else {
                const bool ccw = (T - S).cross(E - T) > 0;
                const Vec2 from = ccw ? S : E, to = ccw ? E : S;
                const double r = distance(C, S), a0 = (from - C).angle();
                double sw = cad::normAngle((to - C).angle() - a0);
                if(sw < 1e-9) sw = 2 * cad::kPi;
                const int n = std::max(8, int(sw / (2 * cad::kPi) * 96));
                for(int i = 0; i < n; ++i) {
                    const double t0 = a0 + sw * i / n, t1 = a0 + sw * (i + 1) / n;
                    ed.previewLines.push_back(
                        {C + Vec2(std::cos(t0), std::sin(t0)) * r, C + Vec2(std::cos(t1), std::sin(t1)) * r});
                }
            }
        }
        ed.refreshView();
    }

    void commit() {
        SketchEditor &ed = editor();
        const Vec2 S = m_start.pos, E = m_end.pos, T = m_snap.pos;
        Vec2 C;
        if(!circumcircle(S, T, E, C)) return;
        const bool ccw = (T - S).cross(E - T) > 0;
        cad::Sketch work = ed.sketch();
        std::vector<PendingConstraint> pending;
        const int ps = ed.pointForSnap(work, m_start, pending);
        const int pe = ed.pointForSnap(work, m_end, pending);
        if(ps == pe) return;
        const int pc = work.addPoint(C.x, C.y);
        if(ccw) work.addArc(pc, ps, pe);
        else work.addArc(pc, pe, ps);
        ed.commit(tr("Arc"), std::move(work), pending);
        m_state = 0;
        ed.clearPreview();
    }

    int m_state = 0;
    SketchSnap m_start, m_end, m_snap;
    QPointF m_cursor;
};

// ---------------------------------------------------------------------------
// Point.

class PointTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Point; }
    QString prompt() const override { return tr("Click to place sketch points."); }
    void activate() override { hud().setFields({}); }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        SketchEditor &ed = editor();
        m_snap = ed.snap(e->position());
        if(m_snap.kind == SketchSnap::Kind::Point) return true; // already a point there
        cad::Sketch work = ed.sketch();
        std::vector<PendingConstraint> pending;
        ed.pointForSnap(work, m_snap, pending);
        ed.commit(tr("Point"), std::move(work), pending);
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        m_cursor = e->position();
        m_snap = editor().snap(m_cursor);
        editor().previewSnap = m_snap;
        editor().refreshView();
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override { return commonKey(e); }

protected:
    void paintToolOverlay(QPainter &p) override {
        if(auto id = snapHint(m_snap)) paintHint(p, m_cursor, *id);
    }

private:
    SketchSnap m_snap;
    QPointF m_cursor;
};

// ---------------------------------------------------------------------------
// Move / Copy: pick the geometry (or use the selection), click a base point,
// then where it goes; the heads-up boxes take an exact shift and a turn about
// the base point. Hold Ctrl (Cmd) on the last click, or press Enter with Ctrl,
// to leave the original and place a copy.

class MoveTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Move; }
    QString prompt() const override {
        switch(m_phase) {
        case Phase::Pick: return tr("Click the geometry to move (Enter when done).");
        case Phase::Base: return tr("Click the base point to move from.");
        case Phase::Target:
            return tr("Click where it goes, or type the shift and turn; hold Ctrl (Cmd) to place a copy instead.");
        }
        return {};
    }
    void activate() override {
        hud().setFields({{tr("ΔX"), cad::ValueKind::Length}, {tr("ΔY"), cad::ValueKind::Length},
                         {tr("Turn"), cad::ValueKind::Angle}});
        hud().hide();
        m_phase = picked().empty() ? Phase::Pick : Phase::Base;
        m_mode.showStatus(prompt());
    }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        SketchEditor &ed = editor();
        if(m_phase == Phase::Pick) {
            const SketchHit hit = ed.hitTest(e->position(), HitPoints | HitCurves);
            if(hit.kind == HitKind::Point || hit.kind == HitKind::Curve) ed.select(hit, true);
            return true;
        }
        update(e->position());
        if(m_phase == Phase::Base) {
            m_base = m_snap.pos;
            m_phase = Phase::Target;
            hud().unlockAll();
            update(e->position());
            m_mode.showStatus(prompt());
        } else {
            commit(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
        }
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        if(m_phase == Phase::Pick) {
            setHover(e->position(), HitPoints | HitCurves);
            return true;
        }
        update(e->position());
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override {
        if(e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            if(m_phase == Phase::Pick && !picked().empty()) {
                m_phase = Phase::Base;
                m_mode.showStatus(prompt());
            } else if(m_phase == Phase::Target) {
                commit(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
            }
            return true;
        }
        return commonKey(e);
    }
    bool cancel() override {
        if(m_phase != Phase::Target) return false;
        m_phase = Phase::Base;
        hud().hide();
        hud().unlockAll();
        editor().clearPreview();
        m_mode.showStatus(prompt());
        return true;
    }
    void hudCommit() override { commit(false); }
    void hudChanged() override { update(m_cursor); }

    // Tests: the same as clicking with typed values.
    void setBase(cad::Vec2 p) {
        m_base = p;
        m_phase = Phase::Target;
    }

protected:
    void paintToolOverlay(QPainter &p) override {
        if(m_phase != Phase::Pick)
            if(auto id = snapHint(m_snap)) paintHint(p, m_cursor, *id);
    }

private:
    enum class Phase { Pick, Base, Target };

    std::set<int> picked() const {
        std::set<int> out;
        for(int id : editor().selectedEntities)
            if(id > 0) out.insert(id);
        return out;
    }

    // The shift and turn: typed values win over the mouse.
    std::pair<Vec2, double> motion() const {
        Vec2 d = m_snap.pos - m_base;
        if(const auto x = hud().value(0)) d.x = *x;
        if(const auto y = hud().value(1)) d.y = *y;
        return {d, hud().value(2).value_or(0.0)};
    }

    void update(QPointF px) {
        m_cursor = px;
        SketchEditor &ed = editor();
        m_snap = ed.snap(px);
        ed.previewLines.clear();
        ed.previewSnap = m_snap;
        if(m_phase == Phase::Target) {
            const auto [d, turn] = motion();
            ed.previewLines = ed.movedOutline(picked(), m_base, d, turn);
            ed.previewLines.push_back({m_base, m_base + d});
            hud().setLive(0, d.x);
            hud().setLive(1, d.y);
            hud().setLive(2, turn);
            const QPointF at = ed.toScreen(m_base + d);
            hud().place(0, at + QPointF(0, 34));
            hud().place(1, at + QPointF(0, 60));
            hud().place(2, at + QPointF(0, 86));
        }
        ed.refreshView();
    }

    void commit(bool copy) {
        if(m_phase != Phase::Target) return;
        const auto [d, turn] = motion();
        SketchEditor &ed = editor();
        const std::set<int> ids = picked();
        ed.clearPreview();
        hud().hide();
        hud().unlockAll();
        if(ed.moveEntities(ids, m_base, d, turn, copy)) m_mode.showStatus(copy ? tr("Copied.") : tr("Moved."));
        m_phase = Phase::Base;
    }

    Phase m_phase = Phase::Pick;
    Vec2 m_base;
    SketchSnap m_snap;
    QPointF m_cursor;
};

// ---------------------------------------------------------------------------
// Offset: click a curve (its whole chain comes along; Alt-click for just that
// curve), then move to the side to offset to and click, or type the distance
// (a negative one goes to the other side) and press Enter. Clicking another
// curve first adds it too.

class OffsetTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Offset; }
    QString prompt() const override {
        return m_phase == Phase::Pick
                   ? tr("Select the curves to offset.")
                   : tr("Move to the side to offset to and click, or type the distance and press Enter; click "
                        "another curve to add it.");
    }
    void activate() override {
        hud().setFields({{tr("Offset"), cad::ValueKind::Length}});
        hud().hide();
        m_picked.clear();
        for(int id : editor().selectedEntities)
            if(const auto *e = editor().sketch().find(id); e && e->isCurve()) m_picked.push_back(id);
        m_phase = m_picked.empty() ? Phase::Pick : Phase::Distance;
        if(m_phase == Phase::Distance) startDistance();
        m_mode.showStatus(prompt());
    }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        const SketchHit hit = editor().hitTest(e->position(), HitCurves);
        const bool newCurve = hit.kind == HitKind::Curve &&
                              std::find(m_picked.begin(), m_picked.end(), hit.id) == m_picked.end();
        if(newCurve) {
            pick(hit.id, e->modifiers() & Qt::AltModifier);
            return true;
        }
        if(m_phase == Phase::Distance) {
            update(e->position());
            commit();
        }
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        setHover(e->position(), HitCurves);
        if(m_phase == Phase::Distance) update(e->position());
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override {
        if((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && m_phase == Phase::Distance) {
            commit();
            return true;
        }
        return commonKey(e);
    }
    bool cancel() override {
        if(m_phase == Phase::Pick) return false;
        reset();
        return true;
    }
    void hudCommit() override { commit(); }
    void hudChanged() override { update(m_cursor); }

    // Tests: the picked curves, and the offset the preview shows (left of the chains).
    const std::vector<int> &picked() const { return m_picked; }
    double leftDistance() const { return m_left; }

protected:
    void paintToolOverlay(QPainter &) override {}

private:
    enum class Phase { Pick, Distance };

    void pick(int curve, bool single) {
        const std::vector<int> add = single ? std::vector<int>{curve} : cad::connectedCurves(editor().sketch(), curve);
        for(int id : add)
            if(std::find(m_picked.begin(), m_picked.end(), id) == m_picked.end()) m_picked.push_back(id);
        editor().selectEntities(m_picked, false);
        if(m_phase == Phase::Pick) {
            m_phase = Phase::Distance;
            startDistance();
        }
        update(m_cursor);
        m_mode.showStatus(prompt());
    }

    void startDistance() {
        hud().unlockAll();
        m_left = 0.0;
    }

    // The distance to the left of the chains: the mouse picks the side (and,
    // until a value is typed, the distance); a typed value goes on that side.
    void update(QPointF px) {
        m_cursor = px;
        SketchEditor &ed = editor();
        ed.previewLines.clear();
        ed.previewConstruction.clear();
        if(m_phase != Phase::Distance) {
            ed.refreshView();
            return;
        }
        std::vector<cad::CurveChain> chains;
        std::string why;
        if(!cad::buildChains(ed.sketch(), m_picked, chains, why)) {
            m_mode.showStatus(QString::fromStdString(why));
            ed.refreshView();
            return;
        }
        double mouse = 0.0;
        if(const auto q = ed.toSketch(px)) mouse = cad::sideDistance(ed.sketch(), chains, *q);
        // Outwards until the mouse is off the curves.
        const double side = std::fabs(mouse) > 1e-9 ? (mouse < 0 ? -1.0 : 1.0) : cad::outwardSign(ed.sketch(), chains.front());
        if(const auto typed = hud().value(0)) m_left = side * *typed;
        else m_left = mouse;
        hud().setLive(0, std::fabs(m_left));
        hud().place(0, px + QPointF(18, 22));
        std::vector<cad::OffsetChain> geometry;
        const bool ok = std::fabs(m_left) > 1e-9 && cad::offsetGeometry(ed.sketch(), chains, m_left, geometry, why);
        // Too big: shown as construction (dashed) where it can be worked out.
        auto &out = ok ? ed.previewLines : ed.previewConstruction;
        for(const auto &g : geometry)
            for(const auto &curve : g.curves) {
                const auto pl = cad::offsetPolyline(curve);
                for(size_t i = 0; i + 1 < pl.size(); ++i) out.push_back({pl[i], pl[i + 1]});
            }
        m_mode.showStatus(ok || std::fabs(m_left) < 1e-9 ? prompt() : QString::fromStdString(why));
        ed.refreshView();
    }

    void commit() {
        if(m_phase != Phase::Distance) return;
        update(m_cursor);
        QString why;
        const auto made = editor().offsetCurves(m_picked, m_left, &why);
        if(made.empty()) {
            m_mode.showStatus(tr("Offset not made: %1.").arg(why));
            return;
        }
        reset();
        m_mode.showStatus(tr("Offset %1 curves by %2 mm.").arg(made.size()).arg(std::fabs(m_left), 0, 'f', 2));
    }

    void reset() {
        m_phase = Phase::Pick;
        m_picked.clear();
        hud().hide();
        hud().unlockAll();
        editor().clearPreview();
        editor().clearSelection();
        m_mode.showStatus(prompt());
    }

    Phase m_phase = Phase::Pick;
    std::vector<int> m_picked;
    double m_left = 0.0;
    QPointF m_cursor;
};

// ---------------------------------------------------------------------------
// Text: click where the text goes and type. The panel sets the text (several
// lines), font, size, where its origin is, its angle and Reverse (mirrored
// letters); the canvas shows it as it will be. A click elsewhere on the
// canvas moves it there. Double-clicking existing text opens it here too.

// The last text's style, for the next one.
struct TextDefaults {
    QString font = QStringLiteral("DejaVu Sans");
    bool bold = false, italic = false;
    double size = 5.0;
};

TextDefaults &textDefaults() {
    static TextDefaults d;
    return d;
}

class TextTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Text; }
    QString prompt() const override {
        return m_open ? tr("Type the text. Enter or OK places it (Shift+Enter for a new line); click to move it.")
                      : tr("Click where the text goes.");
    }
    Qt::CursorShape cursor() const override { return m_open ? Qt::CrossCursor : Qt::IBeamCursor; }
    void activate() override {
        hud().hide();
        m_mode.showStatus(prompt());
    }
    void deactivate() override { close(); }
    bool openEntity(int id) override {
        const SkEntity *e = editor().sketch().find(id);
        if(!e || !e->isText()) return false;
        m_editing = id;
        open(editor().sketch().pointPos(e->a), *e);
        return true;
    }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        m_snap = editor().snap(e->position(), {}, false);
        if(!m_open) {
            SkEntity style;
            style.font = textDefaults().font.toStdString();
            style.bold = textDefaults().bold;
            style.italic = textDefaults().italic;
            style.size = textDefaults().size;
            m_editing = 0;
            open(m_snap.pos, style);
        } else {
            // Moves it where clicked.
            m_x->enterExpression(QString::fromStdString(SketchEditor::formatExpression(m_snap.pos.x, cad::ValueKind::Length)));
            m_y->enterExpression(QString::fromStdString(SketchEditor::formatExpression(m_snap.pos.y, cad::ValueKind::Length)));
            m_text->setFocus();
        }
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        m_cursor = e->position();
        if(!m_open) {
            m_snap = editor().snap(e->position(), {}, false);
            editor().previewSnap = m_snap;
            editor().refreshView();
        }
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override {
        if(m_open && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
            place();
            return true;
        }
        // Typing on the canvas goes into the text.
        if(m_open && !e->text().isEmpty() && e->text().at(0).isPrint() &&
           !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier))) {
            m_text->setFocus();
            m_text->insertPlainText(e->text());
            return true;
        }
        return commonKey(e);
    }
    bool cancel() override {
        if(!m_open) return false;
        close();
        m_mode.showStatus(prompt());
        return true;
    }

    // Tests: the panel's widgets.
    QPlainTextEdit *textBox() const { return m_text; }

private:
    CommandPanel *panel() const { return m_mode.commandPanel(); }

    void open(Vec2 at, const SkEntity &style) {
        CommandPanel *p = panel();
        if(!p) return;
        close();
        m_open = true;
        // The panel goes where the sketch palette is.
        if(SketchPalette *pal = m_mode.palette()) {
            m_paletteShown = pal->isVisible();
            pal->hide();
        }
        const auto eval = [this](const std::string &expr, cad::ValueKind k) { return editor().evaluate(expr, k); };
        p->begin(m_editing ? tr("Edit Text") : tr("Text"), IconId::Text);
        m_text = p->addTextBox(tr("Text"), "sketchTextText");
        QStringList fonts;
        for(const std::string &f : cad::availableFonts()) fonts << QString::fromStdString(f);
        const QString font = QString::fromStdString(style.font);
        if(!fonts.contains(font)) fonts.prepend(font);
        m_font = p->addChoice(tr("Font"), fonts, "sketchTextFont");
        m_font->setCurrentText(font);
        m_font->setMaxVisibleItems(16);
        m_bold = p->addCheck(tr("Bold"), "sketchTextBold");
        m_bold->setChecked(style.bold);
        m_italic = p->addCheck(tr("Italic"), "sketchTextItalic");
        m_italic->setChecked(style.italic);
        m_size = p->addValue(tr("Size"), cad::ValueKind::Length, eval, "sketchTextSize");
        m_size->setExpression(QString::fromStdString(SketchEditor::formatExpression(style.size, cad::ValueKind::Length)));
        m_x = p->addValue(tr("X"), cad::ValueKind::Length, eval, "sketchTextX");
        m_x->setExpression(QString::fromStdString(SketchEditor::formatExpression(at.x, cad::ValueKind::Length)));
        m_y = p->addValue(tr("Y"), cad::ValueKind::Length, eval, "sketchTextY");
        m_y->setExpression(QString::fromStdString(SketchEditor::formatExpression(at.y, cad::ValueKind::Length)));
        m_angle = p->addAngle(tr("Angle"), eval, "sketchTextAngle", QColor(229, 70, 58));
        m_angle->setExpression(
            QString::fromStdString(SketchEditor::formatExpression(style.angle * cad::kPi / 180.0, cad::ValueKind::Angle)));
        m_mirror = p->addCheck(tr("Reverse"), "sketchTextReverse");
        m_mirror->setToolTip(tr("Mirrors the letters, so they read the right way from the other side (stamps, "
                                "moulds, text printed face down)."));
        m_mirror->setChecked(style.mirror);
        m_text->setPlainText(QString::fromStdString(style.text));
        m_text->selectAll();
        // Connected through an object of this tool's own, so nothing calls
        // into it once it is closed or gone.
        m_link = std::make_unique<QObject>();
        QObject *ctx = m_link.get();
        auto changed = [this] { preview(); };
        m_connections.push_back(QObject::connect(m_text, &QPlainTextEdit::textChanged, ctx, changed));
        m_connections.push_back(QObject::connect(m_font, &QComboBox::currentTextChanged, ctx, changed));
        for(QCheckBox *c : {m_bold, m_italic, m_mirror})
            m_connections.push_back(QObject::connect(c, &QCheckBox::toggled, ctx, changed));
        for(ValueField *f : {m_size, m_x, m_y, m_angle})
            m_connections.push_back(QObject::connect(f, &ValueField::revalidated, ctx, changed));
        m_connections.push_back(QObject::connect(p, &CommandPanel::accepted, ctx, [this] { place(); }));
        m_connections.push_back(QObject::connect(p, &CommandPanel::cancelled, ctx, [this] { cancel(); }));
        m_text->setFocus();
        preview();
        m_mode.showStatus(prompt());
    }

    void close() {
        for(const auto &c : m_connections) QObject::disconnect(c);
        m_connections.clear();
        // Deleted from the event loop: close() may run inside one of its handlers.
        if(m_link) m_link.release()->deleteLater();
        if(m_open && panel()) panel()->end();
        if(m_open && m_paletteShown)
            if(SketchPalette *pal = m_mode.palette()) pal->show();
        m_open = false;
        m_paletteShown = false;
        m_text = nullptr;
        editor().clearPreview();
    }

    // The text as the panel has it (false, and why, if a value is wrong).
    bool gather(SkEntity &e, Vec2 &at, QString &why) const {
        e.text = m_text->toPlainText().toStdString();
        e.font = m_font->currentText().toStdString();
        e.bold = m_bold->isChecked();
        e.italic = m_italic->isChecked();
        e.mirror = m_mirror->isChecked();
        if(!m_size->valid() || !m_x->valid() || !m_y->valid() || !m_angle->valid()) {
            why = tr("Check the values in red.");
            return false;
        }
        e.size = *m_size->value();
        if(e.size <= 0.0) {
            why = tr("The size must be more than 0.");
            return false;
        }
        e.angle = *m_angle->value() * 180.0 / cad::kPi;
        at = {*m_x->value(), *m_y->value()};
        return true;
    }

    // The sketch with the text as the panel has it (and its id there).
    int trial(cad::Sketch &s, const SkEntity &e, Vec2 at) const {
        int id = m_editing;
        if(!id || !s.find(id)) id = s.addText(s.addPoint(at.x, at.y), e.text, e.construction);
        SkEntity &t = *s.find(id);
        t.text = e.text;
        t.font = e.font;
        t.bold = e.bold;
        t.italic = e.italic;
        t.mirror = e.mirror;
        t.size = e.size;
        t.angle = e.angle;
        if(SkEntity *o = s.find(t.a)) {
            o->x = at.x;
            o->y = at.y;
        }
        return id;
    }

    void preview() {
        if(!m_open || !m_text) return;
        SketchEditor &ed = editor();
        ed.previewLines.clear();
        ed.previewSnap.reset();
        ed.previewHidden.clear();
        if(m_editing) ed.previewHidden.insert(m_editing);
        SkEntity e;
        Vec2 at;
        QString why;
        if(!gather(e, at, why)) {
            panel()->setMessage(why, cad::Severity::Error);
            ed.refreshView();
            return;
        }
        cad::Sketch s = ed.sketch();
        const int id = trial(s, e, at);
        const cad::SketchTextLetters letters = cad::sketchTextLetters(s, *s.find(id));
        for(const auto &piece : letters.pieces)
            for(const auto &loop : piece)
                for(size_t i = 0; i < loop.size(); ++i) ed.previewLines.push_back({loop[i], loop[(i + 1) % loop.size()]});
        ed.previewPoints = {at};
        if(e.text.find_first_not_of(" \t\r\n") == std::string::npos) panel()->setMessage(tr("Type the text."));
        else if(!letters.ok) panel()->setMessage(QString::fromStdString(letters.error), cad::Severity::Error);
        else if(!letters.warning.empty()) panel()->setMessage(QString::fromStdString(letters.warning), cad::Severity::Warning);
        else panel()->setMessage({});
        ed.refreshView();
    }

    void place() {
        if(!m_open) return;
        SkEntity e;
        Vec2 at;
        QString why;
        if(!gather(e, at, why)) {
            panel()->setMessage(why, cad::Severity::Error);
            return;
        }
        if(e.text.find_first_not_of(" \t\r\n") == std::string::npos) {
            panel()->setMessage(tr("Type the text first."), cad::Severity::Error);
            m_text->setFocus();
            return;
        }
        TextDefaults &d = textDefaults();
        d.font = QString::fromStdString(e.font);
        d.bold = e.bold;
        d.italic = e.italic;
        d.size = e.size;
        const int editing = m_editing;
        const bool ok = editor().edit(editing ? tr("Edit Text") : tr("Text"),
                                      [&](cad::Sketch &s) { trial(s, e, at); }, false);
        close();
        m_editing = 0;
        m_mode.showStatus(ok ? (editing ? tr("Text changed.") : tr("Text placed. Click to place more text."))
                             : tr("The text could not be placed there."));
    }

    bool m_open = false, m_paletteShown = false;
    int m_editing = 0;
    SketchSnap m_snap;
    QPointF m_cursor;
    QPlainTextEdit *m_text = nullptr;
    QComboBox *m_font = nullptr;
    QCheckBox *m_bold = nullptr, *m_italic = nullptr, *m_mirror = nullptr;
    ValueField *m_size = nullptr, *m_x = nullptr, *m_y = nullptr, *m_angle = nullptr;
    std::unique_ptr<QObject> m_link;
    std::vector<QMetaObject::Connection> m_connections;
};

// ---------------------------------------------------------------------------
// Sketch Dimension: pick one or two entities, then click to place the label;
// the value box opens right away, as in Fusion 360.

class DimensionTool final : public SketchTool {
public:
    using SketchTool::SketchTool;
    SketchToolKind kind() const override { return SketchToolKind::Dimension; }
    QString prompt() const override {
        return m_picks.empty() ? tr("Select a line, circle, arc, or two points / lines to dimension.")
                               : tr("Select another entity, or click to place the dimension.");
    }
    void activate() override { hud().setFields({}); }

    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        const QPointF px = e->position();
        const auto q = editor().toSketch(px);
        if(!q) return true;
        const SketchHit hit = editor().hitTest(px, HitGeometry);
        if(hit.valid() && std::find(m_picks.begin(), m_picks.end(), hit) == m_picks.end() && m_picks.size() < 2) {
            auto next = m_picks;
            next.push_back(hit);
            if(spec(next, *q).valid || awaitingSecond(next)) {
                m_picks = next;
                m_mode.showStatus(prompt());
                editor().refreshView();
                return true;
            }
        }
        const Spec sp = spec(m_picks, *q);
        if(sp.valid) place(sp, *q);
        else if(!hit.valid()) m_picks.clear();
        m_mode.showStatus(prompt());
        editor().refreshView();
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        m_cursor = e->position();
        setHover(m_cursor, HitGeometry);
        editor().refreshView();
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override { return commonKey(e); }
    bool cancel() override {
        if(m_picks.empty()) return false;
        m_picks.clear();
        editor().refreshView();
        m_mode.showStatus(prompt());
        return true;
    }

protected:
    void contributeTool(RenderScene &scene) override { highlight(scene, m_picks, kPickColor); }
    void paintToolOverlay(QPainter &p) override {
        const auto q = editor().toSketch(m_cursor);
        if(!q) return;
        const Spec sp = spec(m_picks, *q);
        if(!sp.valid) return;
        cad::SkConstraint c;
        c.id = -1000;
        c.type = sp.type;
        c.e1 = sp.e1;
        c.e2 = sp.e2;
        c.supplementary = sp.supplementary;
        c.driven = false;
        c.label = *q - editor().dimensionAnchor(c);
        editor().paintDimensionPreview(p, c);
    }

private:
    struct Spec {
        bool valid = false;
        SkCon type = SkCon::Distance;
        int e1 = 0, e2 = 0;
        bool supplementary = false;
    };
    enum class Role { None, Point, Line, Axis, Circle, Arc };

    Role role(const SketchHit &h) const {
        switch(h.kind) {
        case HitKind::Point:
        case HitKind::Origin: return Role::Point;
        case HitKind::Axis: return Role::Axis;
        case HitKind::Curve:
            if(const SkEntity *e = editor().sketch().find(h.id)) {
                if(e->type == SkType::Line) return Role::Line;
                if(e->type == SkType::Circle) return Role::Circle;
                if(e->type == SkType::Arc) return Role::Arc;
            }
            return Role::None;
        default: return Role::None;
        }
    }
    int centreOf(const SketchHit &h) const {
        const SkEntity *e = editor().sketch().find(h.id);
        return e ? e->a : 0;
    }
    bool awaitingSecond(const std::vector<SketchHit> &picks) const {
        return picks.size() == 1 && (role(picks[0]) == Role::Point || role(picks[0]) == Role::Axis);
    }
    // Horizontal / vertical / aligned from where the label is, like Fusion.
    Spec pointPair(int p1, int p2, Vec2 q, int line) const {
        const Vec2 a = editor().posOf(p1), b = editor().posOf(p2);
        Spec s;
        s.valid = distance(a, b) > 1e-9;
        const bool axisAligned = std::fabs(a.x - b.x) < 1e-9 || std::fabs(a.y - b.y) < 1e-9;
        const bool inX = q.x > std::min(a.x, b.x) && q.x < std::max(a.x, b.x);
        const bool inY = q.y > std::min(a.y, b.y) && q.y < std::max(a.y, b.y);
        // Above / below the pair: horizontal; left / right of it: vertical;
        // otherwise aligned.
        if(!axisAligned && inX && !inY) s.type = SkCon::HDistance, s.e1 = p1, s.e2 = p2;
        else if(!axisAligned && inY && !inX) s.type = SkCon::VDistance, s.e1 = p1, s.e2 = p2;
        else if(line) s.type = SkCon::Distance, s.e1 = line;
        else s.type = SkCon::Distance, s.e1 = p1, s.e2 = p2;
        return s;
    }
    Spec spec(const std::vector<SketchHit> &picks, Vec2 q) const {
        Spec s;
        if(picks.size() == 1) {
            const SketchHit &h = picks[0];
            switch(role(h)) {
            case Role::Line: {
                const SkEntity *e = editor().sketch().find(h.id);
                return pointPair(e->a, e->b, q, h.id);
            }
            case Role::Circle: s = {true, SkCon::Diameter, h.id}; return s;
            case Role::Arc: s = {true, SkCon::Radius, h.id}; return s;
            default: return s;
            }
        }
        if(picks.size() != 2) return s;
        const SketchHit &h1 = picks[0], &h2 = picks[1];
        const Role r1 = role(h1), r2 = role(h2);
        auto asPoint = [&](const SketchHit &h, Role r) {
            return r == Role::Point ? h.id : (r == Role::Circle || r == Role::Arc) ? centreOf(h) : 0;
        };
        auto isLinear = [](Role r) { return r == Role::Line || r == Role::Axis; };
        const int p1 = asPoint(h1, r1), p2 = asPoint(h2, r2);
        if(p1 && p2) return pointPair(p1, p2, q, 0);
        if(p1 && isLinear(r2)) return {true, SkCon::PointLineDistance, p1, h2.id};
        if(p2 && isLinear(r1)) return {true, SkCon::PointLineDistance, p2, h1.id};
        if(isLinear(r1) && isLinear(r2)) {
            Vec2 a1, b1, a2, b2;
            auto ends = [&](const SketchHit &h, Vec2 &a, Vec2 &b) {
                if(h.kind == HitKind::Axis) {
                    a = Vec2();
                    b = h.id == cad::kSketchXAxis ? Vec2(1, 0) : Vec2(0, 1);
                    return;
                }
                const SkEntity *e = editor().sketch().find(h.id);
                a = editor().posOf(e->a);
                b = editor().posOf(e->b);
            };
            ends(h1, a1, b1);
            ends(h2, a2, b2);
            const Vec2 d1 = (b1 - a1).normalized(), d2 = (b2 - a2).normalized();
            if(std::fabs(d1.cross(d2)) < 1e-9) {
                // Parallel lines: distance from a point of one to the other.
                if(h1.kind == HitKind::Curve) return {true, SkCon::PointLineDistance, editor().sketch().find(h1.id)->a, h2.id};
                if(h2.kind == HitKind::Curve) return {true, SkCon::PointLineDistance, editor().sketch().find(h2.id)->a, h1.id};
                return s;
            }
            // Angle in the sector the cursor is in.
            const double den = (b1 - a1).cross(b2 - a2);
            const Vec2 I = a1 + (b1 - a1) * ((a2 - a1).cross(b2 - a2) / den);
            const Vec2 l = (q - I).length() > 1e-12 ? (q - I).normalized() : (d1 + d2).normalized();
            double best = -2;
            bool supp = false;
            for(int s1 : {1, -1})
                for(int s2 : {1, -1}) {
                    const double d = (d1 * s1 + d2 * s2).normalized().dot(l);
                    if(d > best) {
                        best = d;
                        supp = s1 * s2 < 0;
                    }
                }
            return {true, SkCon::Angle, h1.id, h2.id, supp};
        }
        return s;
    }

    void place(const Spec &sp, Vec2 q) {
        cad::SkConstraint c;
        c.type = sp.type;
        c.e1 = sp.e1;
        c.e2 = sp.e2;
        c.supplementary = sp.supplementary;
        const Vec2 label = q - editor().dimensionAnchor(c);
        bool driven = false;
        const int id = editor().addDimension(sp.type, sp.e1, sp.e2, label, sp.supplementary, &driven);
        m_picks.clear();
        if(!driven) m_mode.editDimension(id);
    }

    std::vector<SketchHit> m_picks;
    QPointF m_cursor;
};

// ---------------------------------------------------------------------------
// Geometric constraints: pick the entities they relate.

class ConstraintTool final : public SketchTool {
public:
    ConstraintTool(SketchMode &mode, SketchToolKind kind) : SketchTool(mode), m_kind(kind) {}
    SketchToolKind kind() const override { return m_kind; }
    QString prompt() const override {
        switch(m_kind) {
        case SketchToolKind::Coincident: return tr("Select two points, or a point and a curve.");
        case SketchToolKind::HorizontalVertical: return tr("Select a line, or two points.");
        case SketchToolKind::Parallel: return tr("Select two lines to make parallel.");
        case SketchToolKind::Perpendicular: return tr("Select two lines to make perpendicular.");
        case SketchToolKind::Tangent: return tr("Select a line and an arc or circle, or two arcs / circles.");
        case SketchToolKind::Equal: return tr("Select two lines, or two arcs / circles.");
        case SketchToolKind::Midpoint: return tr("Select a point and a line.");
        case SketchToolKind::Concentric: return tr("Select two arcs or circles.");
        case SketchToolKind::Fix: return tr("Select points or curves to fix or unfix.");
        case SketchToolKind::Symmetric: return tr("Select two points, then the symmetry line.");
        default: return {};
        }
    }
    void activate() override {
        hud().setFields({});
        // Apply straight away to a suitable selection, like Fusion.
        std::vector<SketchHit> sel;
        for(int id : editor().selectedEntities) {
            SketchHit h;
            if(id == cad::kSketchOrigin) h = {HitKind::Origin, id};
            else if(id == cad::kSketchXAxis || id == cad::kSketchYAxis) h = {HitKind::Axis, id};
            else if(const SkEntity *e = editor().sketch().find(id)) h = {e->isCurve() ? HitKind::Curve : HitKind::Point, id};
            if(h.valid()) sel.push_back(h);
        }
        if(sel.empty()) return;
        m_picks = sel;
        if(evaluate() != Result::Applied) m_picks.clear();
        else editor().clearSelection();
    }
    bool mousePress(QMouseEvent *e) override {
        if(e->button() != Qt::LeftButton) return false;
        const SketchHit hit = editor().hitTest(e->position(), HitGeometry);
        if(!hit.valid()) {
            m_picks.clear();
            editor().refreshView();
            return true;
        }
        if(std::find(m_picks.begin(), m_picks.end(), hit) == m_picks.end()) m_picks.push_back(hit);
        if(evaluate() == Result::Invalid) {
            m_picks.clear();
            m_mode.showStatus(tr("That selection does not suit %1. %2").arg(sketchToolName(m_kind), prompt()));
        }
        editor().refreshView();
        return true;
    }
    bool mouseMove(QMouseEvent *e) override {
        setHover(e->position(), HitGeometry);
        return true;
    }
    bool mouseRelease(QMouseEvent *) override { return true; }
    bool keyPress(QKeyEvent *e) override { return commonKey(e); }
    bool cancel() override {
        if(m_picks.empty()) return false;
        m_picks.clear();
        editor().refreshView();
        return true;
    }

protected:
    void contributeTool(RenderScene &scene) override { highlight(scene, m_picks, kPickColor); }

private:
    enum class Result { NeedMore, Applied, Invalid };
    enum class Role { None, Point, Line, Axis, Round };

    Role role(const SketchHit &h) const {
        switch(h.kind) {
        case HitKind::Point:
        case HitKind::Origin: return Role::Point;
        case HitKind::Axis: return Role::Axis;
        case HitKind::Curve:
            if(const SkEntity *e = editor().sketch().find(h.id))
                return e->type == SkType::Line ? Role::Line : Role::Round;
            return Role::None;
        default: return Role::None;
        }
    }

    bool apply(SkCon type, int e1, int e2 = 0, int e3 = 0) {
        m_picks.clear();
        return editor().edit(QString::fromLatin1(cad::toString(type)),
                             [&](cad::Sketch &s) { s.addConstraint(type, e1, e2, e3); });
    }

    Result evaluate() {
        const auto &p = m_picks;
        std::vector<Role> r;
        for(const auto &h : p) r.push_back(role(h));
        auto count = [&](Role x) { return std::count(r.begin(), r.end(), x); };
        auto first = [&](std::initializer_list<Role> roles) -> int {
            for(size_t i = 0; i < p.size(); ++i)
                if(std::find(roles.begin(), roles.end(), r[i]) != roles.end()) return p[i].id;
            return 0;
        };
        const size_t n = p.size();
        const bool linear2 = n == 2 && (r[0] == Role::Line || r[0] == Role::Axis) && (r[1] == Role::Line || r[1] == Role::Axis);
        switch(m_kind) {
        case SketchToolKind::Coincident:
            if(n < 2) return r[0] == Role::None ? Result::Invalid : Result::NeedMore;
            if(n == 2 && count(Role::Point) == 2) return apply(SkCon::Coincident, p[0].id, p[1].id), Result::Applied;
            if(n == 2 && count(Role::Point) == 1)
                return apply(SkCon::PointOnCurve, first({Role::Point}), first({Role::Line, Role::Axis, Role::Round})),
                       Result::Applied;
            return Result::Invalid;
        case SketchToolKind::HorizontalVertical: {
            if(n == 1 && r[0] == Role::Line) {
                const SkEntity *e = editor().sketch().find(p[0].id);
                const Vec2 d = editor().posOf(e->b) - editor().posOf(e->a);
                return apply(std::fabs(d.x) >= std::fabs(d.y) ? SkCon::Horizontal : SkCon::Vertical, p[0].id),
                       Result::Applied;
            }
            if(n == 1 && r[0] == Role::Point) return Result::NeedMore;
            if(n == 2 && count(Role::Point) == 2) {
                const Vec2 d = editor().posOf(p[1].id) - editor().posOf(p[0].id);
                return apply(std::fabs(d.x) >= std::fabs(d.y) ? SkCon::Horizontal : SkCon::Vertical, p[0].id, p[1].id),
                       Result::Applied;
            }
            return Result::Invalid;
        }
        case SketchToolKind::Parallel:
        case SketchToolKind::Perpendicular:
            if(n == 1) return (r[0] == Role::Line || r[0] == Role::Axis) ? Result::NeedMore : Result::Invalid;
            if(linear2)
                return apply(m_kind == SketchToolKind::Parallel ? SkCon::Parallel : SkCon::Perpendicular, p[0].id, p[1].id),
                       Result::Applied;
            return Result::Invalid;
        case SketchToolKind::Tangent:
            if(n == 1) return (r[0] == Role::Line || r[0] == Role::Round) ? Result::NeedMore : Result::Invalid;
            if(n == 2 && count(Role::Round) >= 1 && count(Role::Round) + count(Role::Line) == 2)
                return apply(SkCon::Tangent, p[0].id, p[1].id), Result::Applied;
            return Result::Invalid;
        case SketchToolKind::Equal:
            if(n == 1) return (r[0] == Role::Line || r[0] == Role::Round) ? Result::NeedMore : Result::Invalid;
            if(n == 2 && (count(Role::Line) == 2 || count(Role::Round) == 2))
                return apply(SkCon::Equal, p[0].id, p[1].id), Result::Applied;
            return Result::Invalid;
        case SketchToolKind::Midpoint:
            if(n == 1) return (r[0] == Role::Point || r[0] == Role::Line) ? Result::NeedMore : Result::Invalid;
            if(n == 2 && count(Role::Point) == 1 && count(Role::Line) == 1)
                return apply(SkCon::Midpoint, first({Role::Point}), first({Role::Line})), Result::Applied;
            return Result::Invalid;
        case SketchToolKind::Concentric:
            if(n == 1) return r[0] == Role::Round ? Result::NeedMore : Result::Invalid;
            if(n == 2 && count(Role::Round) == 2) return apply(SkCon::Concentric, p[0].id, p[1].id), Result::Applied;
            return Result::Invalid;
        case SketchToolKind::Fix: {
            std::vector<int> points;
            for(size_t i = 0; i < n; ++i) {
                if(p[i].kind == HitKind::Point) points.push_back(p[i].id);
                else if(p[i].kind == HitKind::Curve)
                    if(const SkEntity *e = editor().sketch().find(p[i].id))
                        for(int q : {e->a, e->b, e->c})
                            if(q) points.push_back(q);
            }
            if(points.empty()) return Result::Invalid;
            m_picks.clear();
            editor().edit(tr("Fix/Unfix"), [&](cad::Sketch &s) {
                bool allFixed = true;
                for(int q : points) {
                    bool fixed = false;
                    for(const auto &c : s.constraints) fixed |= c.type == SkCon::Fix && c.e1 == q;
                    allFixed &= fixed;
                }
                for(int q : points) {
                    std::vector<int> existing;
                    for(const auto &c : s.constraints)
                        if(c.type == SkCon::Fix && c.e1 == q) existing.push_back(c.id);
                    if(allFixed) {
                        for(int id : existing) s.removeConstraint(id);
                    } else if(existing.empty()) {
                        s.addConstraint(SkCon::Fix, q);
                    }
                }
            });
            return Result::Applied;
        }
        case SketchToolKind::Symmetric:
            if(n < 3) {
                if(count(Role::Round) > 0) return Result::Invalid;
                if(count(Role::Line) + count(Role::Axis) > 1) return Result::Invalid;
                return Result::NeedMore;
            }
            if(n == 3 && count(Role::Point) == 2 && count(Role::Line) + count(Role::Axis) == 1) {
                std::vector<int> pts;
                for(size_t i = 0; i < 3; ++i)
                    if(r[i] == Role::Point) pts.push_back(p[i].id);
                return apply(SkCon::Symmetric, pts[0], pts[1], first({Role::Line, Role::Axis})), Result::Applied;
            }
            return Result::Invalid;
        default:
            return Result::Invalid;
        }
    }

    SketchToolKind m_kind;
    std::vector<SketchHit> m_picks;
};

} // namespace

std::unique_ptr<SketchTool> createSketchTool(SketchMode &mode, SketchToolKind kind) {
    switch(kind) {
    case SketchToolKind::Select: return std::make_unique<SelectTool>(mode);
    case SketchToolKind::Line: return std::make_unique<LineTool>(mode);
    case SketchToolKind::Rectangle: return std::make_unique<RectangleTool>(mode, false);
    case SketchToolKind::CenterRectangle: return std::make_unique<RectangleTool>(mode, true);
    case SketchToolKind::Circle: return std::make_unique<CircleTool>(mode);
    case SketchToolKind::Arc: return std::make_unique<ArcTool>(mode);
    case SketchToolKind::Point: return std::make_unique<PointTool>(mode);
    case SketchToolKind::Dimension: return std::make_unique<DimensionTool>(mode);
    case SketchToolKind::Move: return std::make_unique<MoveTool>(mode);
    case SketchToolKind::Offset: return std::make_unique<OffsetTool>(mode);
    case SketchToolKind::Text: return std::make_unique<TextTool>(mode);
    default: return std::make_unique<ConstraintTool>(mode, kind);
    }
}

} // namespace cadjitsu
