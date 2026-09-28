#include "command/PlaneGizmo.h"

#include "ui/AngleDial.h"
#include "viewport/Viewport.h"

#include <QLineF>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace cadjitsu {

namespace {

// Ring radii on screen (logical pixels): the outer one turns about X.
constexpr double kRadiusPx[2] = {80.0, 62.0};
constexpr double kKnobGrabPx = 11.0, kRingGrabPx = 6.0;
constexpr int kCircleSegments = 120;

Camera cameraOf(const Viewport *vp) {
    Camera c = vp->camera();
    c.viewport = vp->size();
    return c;
}

QColor withAlpha(QColor c, int a) {
    c.setAlpha(a);
    return c;
}

QString degreesText(double radians) {
    const double d = std::round(radians * 1800.0 / M_PI) / 10.0;
    const bool whole = std::fabs(d - std::round(d)) < 1e-9;
    QString t = QString::number(d, 'f', whole ? 0 : 1);
    if(t == QStringLiteral("-0")) t = QStringLiteral("0");
    return t.replace(QLatin1Char('-'), QChar(0x2212)) + QChar(0x00B0);
}

// Degrees, snapped to `step` and wrapped to (-180, 180], back in radians.
double snapped(double radians, double stepDeg) {
    const double d = wrapDegrees(radians * 180.0 / M_PI);
    return wrapDegrees(std::round(d / stepDeg) * stepDeg) * M_PI / 180.0;
}

} // namespace

PlaneGizmo::PlaneGizmo(Viewport *viewport) : m_viewport(viewport), m_arrow(viewport) {
    m_rings[0].color = QColor(229, 70, 58);
    m_rings[0].name = QObject::tr("Tilt X");
    m_rings[1].color = QColor(61, 174, 79);
    m_rings[1].name = QObject::tr("Tilt Y");
}

double PlaneGizmo::radius(int i) const {
    const Camera c = cameraOf(m_viewport);
    return c.unitsPerPixel(std::max(c.depthOf(ring(i).center), 1e-3f)) * kRadiusPx[i];
}

QVector3D PlaneGizmo::knobPoint(int i) const {
    const Ring &r = ring(i);
    const QVector3D side = QVector3D::crossProduct(r.axis, r.zero);
    return r.center + (r.zero * float(std::cos(r.angle)) + side * float(std::sin(r.angle))) * float(radius(i));
}

QPointF PlaneGizmo::knobOnScreen(int i) const { return cameraOf(m_viewport).project(knobPoint(i)); }

QPointF PlaneGizmo::ringPointOnScreen(int i, double angle) const {
    const Ring &r = ring(i);
    const QVector3D side = QVector3D::crossProduct(r.axis, r.zero);
    return cameraOf(m_viewport)
        .project(r.center + (r.zero * float(std::cos(angle)) + side * float(std::sin(angle))) * float(radius(i)));
}

void PlaneGizmo::setFocus(Part p) {
    if(p == Part::None || p == m_focus) return;
    m_focus = p;
    if(onFocusChanged) onFocusChanged();
}

bool PlaneGizmo::angleAt(int i, QPointF px, double *angle) const {
    const Ring &r = ring(i);
    QVector3D ro, rd;
    cameraOf(m_viewport).ray(px, ro, rd);
    const float den = QVector3D::dotProduct(rd.normalized(), r.axis);
    // A ring seen edge-on cannot be turned by the mouse (use the value box).
    if(std::fabs(den) < 0.03f) return false;
    const float t = QVector3D::dotProduct(r.center - ro, r.axis) / QVector3D::dotProduct(rd, r.axis);
    const QVector3D v = ro + rd * t - r.center;
    const QVector3D side = QVector3D::crossProduct(r.axis, r.zero);
    *angle = std::atan2(QVector3D::dotProduct(v, side), QVector3D::dotProduct(v, r.zero));
    return true;
}

PlaneGizmo::Part PlaneGizmo::hitTest(QPointF px) const {
    // Knobs first, then the arrow, then anywhere on a ring.
    for(int i = 0; i < 2; ++i)
        if(ring(i).visible && QLineF(knobOnScreen(i), px).length() < kKnobGrabPx) return i ? Part::Ring1 : Part::Ring0;
    if(m_arrow.nearHead(px)) return Part::Arrow;
    double best = kRingGrabPx;
    Part hit = Part::None;
    for(int i = 0; i < 2; ++i) {
        if(!ring(i).visible) continue;
        QPointF prev = ringPointOnScreen(i, 0.0);
        for(int k = 1; k <= kCircleSegments / 2; ++k) {
            const QPointF cur = ringPointOnScreen(i, 2.0 * M_PI * k / (kCircleSegments / 2));
            // Distance from px to the segment prev-cur.
            const QPointF d = cur - prev;
            const double len2 = QPointF::dotProduct(d, d);
            const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(px - prev, d) / len2, 0.0, 1.0) : 0.0;
            const double dist = QLineF(prev + d * t, px).length();
            if(dist < best) {
                best = dist;
                hit = i ? Part::Ring1 : Part::Ring0;
            }
            prev = cur;
        }
    }
    return hit;
}

void PlaneGizmo::setHot(Part p) {
    m_hot = p;
    m_arrow.setHot(p == Part::Arrow);
    m_viewport->setCursor(cursor());
}

Qt::CursorShape PlaneGizmo::cursor() const {
    if(m_drag == Part::Ring0 || m_drag == Part::Ring1) return Qt::ClosedHandCursor;
    if(m_hot == Part::Ring0 || m_hot == Part::Ring1) return Qt::OpenHandCursor;
    if(m_hot == Part::Arrow || m_drag == Part::Arrow) return Qt::SizeAllCursor;
    return Qt::ArrowCursor;
}

bool PlaneGizmo::mousePress(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton) return false;
    const Part p = hitTest(e->position());
    if(p == Part::None) return false;
    if(p == Part::Arrow) {
        if(!m_arrow.mousePress(e)) return false;
    } else {
        const int i = p == Part::Ring1 ? 1 : 0;
        double a = 0.0;
        if(!angleAt(i, e->position(), &a)) return false;
        m_grab = a - ring(i).angle;
    }
    m_drag = p;
    setHot(p);
    setFocus(p);
    return true;
}

bool PlaneGizmo::mouseMove(QMouseEvent *e) {
    if(m_drag == Part::Arrow) return m_arrow.mouseMove(e);
    if(m_drag != Part::None) {
        const int i = m_drag == Part::Ring1 ? 1 : 0;
        double a = 0.0;
        if(angleAt(i, e->position(), &a)) {
            const double v = snapped(a - m_grab, (e->modifiers() & Qt::ShiftModifier) ? 15.0 : 1.0);
            if(std::fabs(v - ring(i).angle) > 1e-12) {
                ring(i).angle = v;
                if(onRingDrag) onRingDrag(i, v);
            }
        }
        return true;
    }
    const Part p = hitTest(e->position());
    if(p == m_hot) return false;
    setHot(p);
    setFocus(p);
    return true;
}

bool PlaneGizmo::mouseRelease(QMouseEvent *e) {
    if(m_drag == Part::None || e->button() != Qt::LeftButton) return false;
    if(m_drag == Part::Arrow) m_arrow.mouseRelease(e);
    m_drag = Part::None;
    setHot(hitTest(e->position()));
    if(onRelease) onRelease();
    return true;
}

void PlaneGizmo::contribute(RenderScene &scene) {
    m_arrow.contribute(scene);
    const Part draggedRing = (m_drag == Part::Ring0 || m_drag == Part::Ring1) ? m_drag : Part::None;
    for(int i = 0; i < 2; ++i) {
        const Ring &r = ring(i);
        if(!r.visible) continue;
        const Part me = i ? Part::Ring1 : Part::Ring0;
        const bool hot = m_hot == me || m_drag == me;
        const bool faded = draggedRing != Part::None && draggedRing != me;
        const float R = float(radius(i));
        const float upp = R / float(kRadiusPx[i]);
        const QVector3D side = QVector3D::crossProduct(r.axis, r.zero);
        auto at = [&](double t, float rad) {
            return r.center + (r.zero * float(std::cos(t)) + side * float(std::sin(t))) * rad;
        };
        auto lines = [&](int alpha, float width) {
            LineBatch b;
            b.color = withAlpha(r.color, faded ? alpha / 3 : alpha);
            b.width = width;
            b.depthTest = false;
            b.ignoreClip = true;
            return b;
        };

        // The ring's disc, faintly, while it is hot.
        if(hot) {
            TriangleBatch disc;
            disc.color = withAlpha(r.color, 22);
            disc.depthTest = false;
            for(int k = 0; k < kCircleSegments; ++k) {
                const double t0 = 2.0 * M_PI * k / kCircleSegments, t1 = 2.0 * M_PI * (k + 1) / kCircleSegments;
                disc.triangles.insert(disc.triangles.end(), {r.center, at(t0, R), at(t1, R)});
            }
            scene.triangles.push_back(disc);
        }
        // The circle.
        LineBatch circle = lines(hot ? 235 : 190, hot ? 2.2f : 1.6f);
        for(int k = 0; k < kCircleSegments; ++k) {
            circle.segments.push_back(at(2.0 * M_PI * k / kCircleSegments, R));
            circle.segments.push_back(at(2.0 * M_PI * (k + 1) / kCircleSegments, R));
        }
        scene.lines.push_back(circle);
        // Ticks every 15 degrees, longer at 45 and longest at 90.
        LineBatch ticks = lines(hot ? 220 : 170, 1.3f);
        for(int deg = 0; deg < 360; deg += 15) {
            const float len = upp * (deg % 90 == 0 ? 12.0f : deg % 45 == 0 ? 8.0f : 5.0f);
            const double t = deg * M_PI / 180.0;
            ticks.segments.push_back(at(t, R));
            ticks.segments.push_back(at(t, R - len));
        }
        scene.lines.push_back(ticks);
        // The zero spoke, faint.
        LineBatch zero = lines(110, 1.0f);
        zero.segments = {r.center, at(0.0, R)};
        scene.lines.push_back(zero);

        const double a = r.angle;
        if(std::fabs(a) > 1e-9) {
            // A wedge and a bold arc from 0 to the angle.
            const int n = std::max(2, int(std::ceil(std::fabs(a) / (M_PI / 90.0))));
            TriangleBatch wedge;
            wedge.color = withAlpha(r.color, faded ? 20 : hot ? 80 : 56);
            wedge.depthTest = false;
            LineBatch arc = lines(255, 3.2f);
            for(int k = 0; k < n; ++k) {
                const double t0 = a * k / n, t1 = a * (k + 1) / n;
                wedge.triangles.insert(wedge.triangles.end(), {r.center, at(t0, R), at(t1, R)});
                arc.segments.push_back(at(t0, R));
                arc.segments.push_back(at(t1, R));
            }
            scene.triangles.push_back(wedge);
            scene.lines.push_back(arc);
        }
        // The spoke to the knob.
        LineBatch spoke = lines(235, 1.8f);
        spoke.segments = {r.center, at(a, R)};
        scene.lines.push_back(spoke);
    }
}

void PlaneGizmo::paintOverlay(QPainter &p) {
    m_arrow.paintOverlay(p);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const Camera cam = cameraOf(m_viewport);
    QFont f = p.font();
    const Part draggedRing = (m_drag == Part::Ring0 || m_drag == Part::Ring1) ? m_drag : Part::None;
    for(int i = 0; i < 2; ++i) {
        const Ring &r = ring(i);
        if(!r.visible) continue;
        const Part me = i ? Part::Ring1 : Part::Ring0;
        const bool hot = m_hot == me || m_drag == me;
        const bool faded = draggedRing != Part::None && draggedRing != me;
        const QPointF c = cam.project(r.center);

        // Degree marks around the ring being used.
        if(hot) {
            f.setPixelSize(10);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            const int marks[] = {0, 90, 180, -90};
            for(int deg : marks) {
                const QPointF on = ringPointOnScreen(i, deg * M_PI / 180.0);
                // Not under the knob or the value box beside it.
                if(QLineF(on, knobOnScreen(i)).length() < 34.0) continue;
                QPointF out = on - c;
                const double l = std::hypot(out.x(), out.y());
                if(l < 1.0) continue;
                out /= l;
                const QString t = degreesText(deg * M_PI / 180.0);
                const double w = p.fontMetrics().horizontalAdvance(t) + 6.0, h = 14.0;
                const QPointF mid = on + QPointF(out.x() * (w / 2 + 6), out.y() * (h / 2 + 5));
                const QRectF box(mid.x() - w / 2, mid.y() - h / 2, w, h);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 200));
                p.drawRoundedRect(box, 3, 3);
                p.setPen(r.color.darker(135));
                p.drawText(box, Qt::AlignCenter, t);
            }
        }

        // The knob.
        const QPointF k = knobOnScreen(i);
        const double kr = hot ? 7.5 : 6.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, faded ? 15 : 40));
        p.drawEllipse(k + QPointF(0, 1.2), kr + 1.0, kr + 1.0);
        p.setPen(QPen(hot ? QColor(255, 255, 255) : withAlpha(r.color, faded ? 110 : 255), 2.0));
        p.setBrush(hot ? r.color : QColor(255, 255, 255, faded ? 150 : 255));
        p.drawEllipse(k, kr, kr);

        // A name tag, except on the ring the canvas value box is on.
        if(m_focus == me) continue;
        f.setPixelSize(11);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        const QString t = r.name + QStringLiteral("  ") + degreesText(r.angle);
        QPointF out = k - c;
        const double l = std::hypot(out.x(), out.y());
        out = l > 1.0 ? out / l : QPointF(1, 0);
        const double w = p.fontMetrics().horizontalAdvance(t) + 16.0, h = 18.0;
        const QPointF mid = k + QPointF(out.x() * (w / 2 + kr + 6), out.y() * (h / 2 + kr + 4));
        const QRectF box(mid.x() - w / 2, mid.y() - h / 2, w, h);
        p.setPen(QPen(withAlpha(r.color, faded ? 90 : 220), 1.0));
        p.setBrush(QColor(255, 255, 255, faded ? 150 : 235));
        p.drawRoundedRect(box, 9, 9);
        p.setPen(QColor(24, 34, 48, faded ? 120 : 255));
        p.drawText(box, Qt::AlignCenter, t);
    }
    p.restore();
}

} // namespace cadjitsu
