#include "ui/AngleDial.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <cmath>

namespace cadly {

double wrapDegrees(double d) {
    d = std::fmod(d, 360.0);
    if(d <= -180.0) d += 360.0;
    if(d > 180.0) d -= 360.0;
    return d;
}

AngleDial::AngleDial(const QColor &accent, QWidget *parent) : QWidget(parent), m_accent(accent) {
    setFocusPolicy(Qt::NoFocus); // the keyboard stays in the value box
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Drag to set the angle (hold Shift for 15° steps); scroll for 1° steps; double-click for 0°"));
    setFixedSize(sizeHint());
}

void AngleDial::setAngle(double degrees) {
    if(degrees == m_angle && !m_invalid) return;
    m_angle = degrees;
    m_invalid = false;
    update();
}

void AngleDial::setInvalid(bool on) {
    if(on == m_invalid) return;
    m_invalid = on;
    update();
}

void AngleDial::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = QRectF(rect()).center();
    const double r = std::min(width(), height()) / 2.0 - 5.0;
    const double a = m_invalid ? 0.0 : wrapDegrees(m_angle);
    const bool hot = m_hover || m_dragging;

    // The face: a soft disc with a track ring.
    QRadialGradient face(c, r + 4);
    face.setColorAt(0.0, QColor(255, 255, 255));
    face.setColorAt(1.0, QColor(238, 242, 247));
    p.setPen(QPen(hot ? m_accent.lighter(150) : QColor(203, 210, 220), 1.0));
    p.setBrush(face);
    p.drawEllipse(c, r + 3.5, r + 3.5);

    // Ticks every 15 degrees, longer every 90.
    for(int deg = 0; deg < 360; deg += 15) {
        const double t = deg * M_PI / 180.0;
        const QPointF u(std::cos(t), -std::sin(t));
        const bool major = deg % 90 == 0;
        p.setPen(QPen(major ? QColor(120, 131, 146) : QColor(176, 185, 197), major ? 1.3 : 0.9, Qt::SolidLine,
                      Qt::RoundCap));
        p.drawLine(c + u * (r - (major ? 5.0 : 3.0)), c + u * (r - 0.5));
    }

    if(!m_invalid && std::fabs(a) > 1e-9) {
        // A translucent wedge and an accent arc from 0 to the angle.
        const QRectF box(c.x() - r, c.y() - r, 2 * r, 2 * r);
        QPainterPath wedge(c);
        wedge.arcTo(box, 0.0, a);
        wedge.closeSubpath();
        QColor fill = m_accent;
        fill.setAlpha(46);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(wedge);
        p.setPen(QPen(m_accent, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(box, 0, int(std::lround(a * 16.0)));
    }

    // The zero mark and the knob.
    p.setPen(QPen(QColor(120, 131, 146, 150), 1.0));
    p.drawLine(c, c + QPointF(r - 5.0, 0));
    if(!m_invalid) {
        const double t = a * M_PI / 180.0;
        const QPointF k = c + QPointF(std::cos(t), -std::sin(t)) * r;
        p.setPen(QPen(m_accent, 1.6));
        p.drawLine(c, c + (k - c) * 0.72);
        p.setPen(QPen(m_accent.darker(115), 1.6));
        p.setBrush(hot ? m_accent : QColor(255, 255, 255));
        p.drawEllipse(k, 4.2, 4.2);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(90, 100, 115));
    p.drawEllipse(c, 1.6, 1.6);

    // The value in the middle, on a small plate so it reads over the wedge.
    QFont f = font();
    f.setPixelSize(9);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    const QString text = m_invalid ? QStringLiteral("–") : QStringLiteral("%1°").arg(std::lround(a));
    const double w = p.fontMetrics().horizontalAdvance(text) + 4.0;
    const QRectF plate(c.x() - w / 2, c.y() + 3.0, w, 11.0);
    p.setBrush(QColor(255, 255, 255, 215));
    p.drawRoundedRect(plate, 3, 3);
    p.setPen(QColor(40, 49, 62));
    p.drawText(plate, Qt::AlignCenter, text);
}

void AngleDial::setFromPoint(QPointF pt, Qt::KeyboardModifiers mods) {
    const QPointF v = pt - QRectF(rect()).center();
    if(std::hypot(v.x(), v.y()) < 3.0) return; // too close to the middle to tell
    double d = std::atan2(-v.y(), v.x()) * 180.0 / M_PI;
    const double step = (mods & Qt::ShiftModifier) ? 15.0 : 1.0;
    edit(wrapDegrees(std::round(d / step) * step));
}

void AngleDial::edit(double degrees) {
    if(degrees == m_angle && !m_invalid) return;
    m_angle = degrees;
    m_invalid = false;
    update();
    emit angleEdited(degrees);
}

void AngleDial::mousePressEvent(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
    m_dragging = true;
    setFromPoint(e->position(), e->modifiers());
    update();
}

void AngleDial::mouseMoveEvent(QMouseEvent *e) {
    if(m_dragging) setFromPoint(e->position(), e->modifiers());
}

void AngleDial::mouseReleaseEvent(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(e);
    m_dragging = false;
    update();
}

void AngleDial::mouseDoubleClickEvent(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton) return;
    m_dragging = false;
    edit(0.0);
}

void AngleDial::wheelEvent(QWheelEvent *e) {
    const int dy = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    if(dy == 0) return;
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 15.0 : 1.0;
    const double base = m_invalid ? 0.0 : wrapDegrees(m_angle);
    edit(wrapDegrees(std::round(base / step) * step + (dy > 0 ? step : -step)));
    e->accept();
}

void AngleDial::enterEvent(QEnterEvent *) {
    m_hover = true;
    update();
}

void AngleDial::leaveEvent(QEvent *) {
    m_hover = false;
    update();
}

} // namespace cadly
