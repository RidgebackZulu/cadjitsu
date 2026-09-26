#include "command/Manipulator.h"

#include "viewport/Viewport.h"

#include <QLineF>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace cadly {

namespace {

const QColor kArrow(35, 105, 215);
const QColor kArrowHot(90, 165, 255);

Camera cameraOf(const Viewport *vp) {
    Camera c = vp->camera();
    c.viewport = vp->size();
    return c;
}

} // namespace

DistanceManipulator::DistanceManipulator(Viewport *viewport) : m_viewport(viewport) {}

void DistanceManipulator::setAxis(const QVector3D &origin, const QVector3D &direction) {
    m_origin = origin;
    m_dir = direction.normalized();
}

// A zero distance still shows a short arrow to grab.
double DistanceManipulator::displayLength() const {
    if(std::fabs(m_distance) > 1e-9) return m_distance;
    const Camera c = cameraOf(m_viewport);
    return c.unitsPerPixel(std::max(c.depthOf(m_origin), 1e-3f)) * 30.0;
}

QPointF DistanceManipulator::headOnScreen() const { return cameraOf(m_viewport).project(head()); }

bool DistanceManipulator::nearHead(QPointF px) const {
    if(!m_visible) return false;
    const Camera c = cameraOf(m_viewport);
    const QPointF h = c.project(head()), o = c.project(m_origin);
    if(QLineF(h, px).length() < 14.0) return true;
    // Anywhere on the shaft near the head also grabs it.
    const QLineF shaft(o, h);
    if(shaft.length() < 1.0) return false;
    const QPointF u = (h - o) / shaft.length();
    const double t = QPointF::dotProduct(px - o, u);
    const QPointF foot = o + u * t;
    return t > shaft.length() * 0.6 && t < shaft.length() + 10 && QLineF(foot, px).length() < 6.0;
}

double DistanceManipulator::axisParameter(QPointF px, bool *ok) const {
    const Camera c = cameraOf(m_viewport);
    QVector3D ro, rd;
    c.ray(px, ro, rd);
    // Closest point between the view ray and the axis.
    const QVector3D w = m_origin - ro;
    const float a = QVector3D::dotProduct(m_dir, m_dir), b = QVector3D::dotProduct(m_dir, rd),
                cc = QVector3D::dotProduct(rd, rd), d = QVector3D::dotProduct(m_dir, w),
                e = QVector3D::dotProduct(rd, w);
    const float den = a * cc - b * b;
    *ok = std::fabs(den) > 1e-6f;
    if(!*ok) return 0.0;
    return double((b * e - cc * d) / den);
}

bool DistanceManipulator::mousePress(QMouseEvent *e) {
    if(e->button() != Qt::LeftButton || !nearHead(e->position())) return false;
    bool ok = false;
    const double t = axisParameter(e->position(), &ok);
    if(!ok) return false;
    m_grab = t - displayLength();
    m_dragging = true;
    return true;
}

bool DistanceManipulator::mouseMove(QMouseEvent *e) {
    if(m_dragging) {
        bool ok = false;
        const double t = axisParameter(e->position(), &ok);
        if(ok) {
            double d = t - m_grab;
            d = std::round(d * 100.0) / 100.0; // 0.01 mm steps
            m_distance = d;
            if(onDrag) onDrag(d);
        }
        return true;
    }
    const bool hot = nearHead(e->position());
    if(hot != m_hot) {
        m_hot = hot;
        m_viewport->setCursor(hot ? Qt::SizeAllCursor : Qt::ArrowCursor);
        return true;
    }
    return false;
}

bool DistanceManipulator::mouseRelease(QMouseEvent *e) {
    if(!m_dragging || e->button() != Qt::LeftButton) return false;
    m_dragging = false;
    if(onRelease) onRelease();
    return true;
}

void DistanceManipulator::contribute(RenderScene &scene) {
    if(!m_visible) return;
    const Camera c = cameraOf(m_viewport);
    const QVector3D tip = head();
    const float upp = c.unitsPerPixel(std::max(c.depthOf(tip), 1e-3f));
    const QColor color = (m_hot || m_dragging) ? kArrowHot : kArrow;
    const float dirSign = displayLength() < 0 ? -1.0f : 1.0f;
    const QVector3D along = m_dir * dirSign;
    LineBatch shaft;
    shaft.color = color;
    shaft.width = 2.4f;
    shaft.depthTest = false;
    shaft.ignoreClip = true;
    shaft.segments = {m_origin, tip - along * (upp * 14.0f)};
    scene.lines.push_back(shaft);
    // A cone for the head.
    QVector3D side = QVector3D::crossProduct(along, QVector3D(0, 0, 1));
    if(side.lengthSquared() < 1e-6f) side = QVector3D::crossProduct(along, QVector3D(1, 0, 0));
    side.normalize();
    const QVector3D side2 = QVector3D::crossProduct(along, side).normalized();
    const QVector3D base = tip - along * (upp * 16.0f);
    const float r = upp * 6.0f;
    TriangleBatch cone;
    cone.color = color;
    cone.depthTest = false;
    const int n = 12;
    for(int i = 0; i < n; ++i) {
        const float a0 = float(2 * M_PI * i / n), a1 = float(2 * M_PI * (i + 1) / n);
        const QVector3D p0 = base + (side * std::cos(a0) + side2 * std::sin(a0)) * r;
        const QVector3D p1 = base + (side * std::cos(a1) + side2 * std::sin(a1)) * r;
        cone.triangles.insert(cone.triangles.end(), {tip, p0, p1, base, p1, p0});
    }
    scene.triangles.push_back(cone);
}

void DistanceManipulator::paintOverlay(QPainter &p) {
    if(!m_visible || label.isEmpty()) return;
    const QPointF h = headOnScreen();
    QFont f = p.font();
    f.setPixelSize(12);
    p.setFont(f);
    const QRectF r(h.x() + 14, h.y() - 22, p.fontMetrics().horizontalAdvance(label) + 12, 20);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(120, 150, 200), 1.0));
    p.setBrush(QColor(255, 255, 255, 235));
    p.drawRoundedRect(r, 3, 3);
    p.setPen(QColor(20, 40, 80));
    p.drawText(r, Qt::AlignCenter, label);
}

} // namespace cadly
