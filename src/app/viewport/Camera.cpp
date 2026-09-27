#include "viewport/Camera.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace cadly {

namespace {

QQuaternion lookRotation(QVector3D viewDir, QVector3D upHint) {
    viewDir.normalize();
    if(std::fabs(QVector3D::dotProduct(viewDir, upHint.normalized())) > 0.999f)
        upHint = std::fabs(viewDir.z()) > 0.9f ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
    const QVector3D zAxis = -viewDir; // camera looks down its -Z
    const QVector3D xAxis = QVector3D::crossProduct(upHint, zAxis).normalized();
    const QVector3D yAxis = QVector3D::crossProduct(zAxis, xAxis);
    return QQuaternion::fromAxes(xAxis, yAxis, zAxis).normalized();
}

} // namespace

Camera::Camera() { rotation = orientationFor(StandardView::Home); }

QQuaternion Camera::orientationFor(StandardView v) {
    switch(v) {
    case StandardView::Front: return lookRotation({0, 1, 0}, {0, 0, 1});
    case StandardView::Back: return lookRotation({0, -1, 0}, {0, 0, 1});
    case StandardView::Left: return lookRotation({1, 0, 0}, {0, 0, 1});
    case StandardView::Right: return lookRotation({-1, 0, 0}, {0, 0, 1});
    case StandardView::Top: return lookRotation({0, 0, -1}, {0, 1, 0});
    case StandardView::Bottom: return lookRotation({0, 0, 1}, {0, -1, 0});
    case StandardView::Home: return lookRotation(QVector3D(-1, 1, -0.8f).normalized(), {0, 0, 1});
    }
    return {};
}

QMatrix4x4 Camera::viewMatrix() const {
    QMatrix4x4 m;
    m.lookAt(eye(), target, up());
    return m;
}

float Camera::viewHeightAtTarget() const { return 2.0f * distance * std::tan(qDegreesToRadians(fovY) * 0.5f); }

QMatrix4x4 Camera::projectionMatrix() const {
    QMatrix4x4 p;
    const float aspect = viewport.height() > 0 ? float(viewport.width()) / float(viewport.height()) : 1.0f;
    if(orthographic) {
        const float h = viewHeightAtTarget() * 0.5f;
        const float w = h * aspect;
        p.ortho(-w, w, -h, h, nearPlane, farPlane);
    } else {
        p.perspective(fovY, aspect, nearPlane, farPlane);
    }
    return p;
}

float Camera::unitsPerPixel(float depth) const {
    const float h = std::max(1, viewport.height());
    if(orthographic) return viewHeightAtTarget() / h;
    return 2.0f * std::max(depth, 1e-6f) * std::tan(qDegreesToRadians(fovY) * 0.5f) / h;
}

void Camera::ray(QPointF px, QVector3D &origin, QVector3D &dir) const {
    const float w = std::max(1, viewport.width()), h = std::max(1, viewport.height());
    const float nx = float(px.x()) / w * 2.0f - 1.0f;
    const float ny = 1.0f - float(px.y()) / h * 2.0f;
    if(orthographic) {
        const float halfH = viewHeightAtTarget() * 0.5f, halfW = halfH * w / h;
        origin = eye() + right() * (nx * halfW) + up() * (ny * halfH);
        dir = forward();
        return;
    }
    const float t = std::tan(qDegreesToRadians(fovY) * 0.5f);
    dir = (forward() + right() * (nx * t * w / h) + up() * (ny * t)).normalized();
    origin = eye();
}

QPointF Camera::project(const QVector3D &world, float *depth) const {
    const QVector4D clip = viewProjection() * QVector4D(world, 1.0f);
    if(depth) *depth = depthOf(world);
    const float iw = std::fabs(clip.w()) > 1e-12f ? 1.0f / clip.w() : 0.0f;
    const float nx = clip.x() * iw, ny = clip.y() * iw;
    return QPointF((nx + 1.0f) * 0.5f * viewport.width(), (1.0f - ny) * 0.5f * viewport.height());
}

void Camera::orbit(float dx, float dy, const QVector3D &pivot) {
    const float degPerPixel = 0.4f;
    const QQuaternion yaw = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), -dx * degPerPixel);
    const QQuaternion pitch = QQuaternion::fromAxisAndAngle(right(), -dy * degPerPixel);
    const QQuaternion q = yaw * pitch;
    target = pivot + q.rotatedVector(target - pivot);
    rotation = (q * rotation).normalized();
}

void Camera::pan(QPointF d, const QVector3D &anchor) {
    const float upp = unitsPerPixel(std::max(depthOf(anchor), nearPlane));
    target += (-right() * float(d.x()) + up() * float(d.y())) * upp;
}

void Camera::zoom(float factor, const QVector3D &anchor) {
    if(factor <= 0) return;
    const float newDistance = std::clamp(distance / factor, 1e-3f, 1e6f);
    const float f = distance / newDistance;
    // Scale the view about the anchor so it stays under the cursor.
    target = anchor + (target - anchor) / f;
    distance = newDistance;
}

void Camera::setView(StandardView v) { rotation = orientationFor(v); }

void Camera::setOrientation(const QVector3D &viewDir, const QVector3D &upHint) { rotation = lookRotation(viewDir, upHint); }

void Camera::fit(const Box3 &box, float margin) {
    if(box.isEmpty()) {
        target = QVector3D(0, 0, 0);
        distance = 300.0f;
        return;
    }
    target = box.center();
    const float r = std::max(box.radius(), 1.0f) * margin;
    const float aspect = viewport.height() > 0 ? float(viewport.width()) / float(viewport.height()) : 1.0f;
    const float halfFov = qDegreesToRadians(fovY) * 0.5f;
    const float fitH = r / std::sin(halfFov);
    const float fitW = r / std::sin(std::atan(std::tan(halfFov) * aspect));
    distance = std::max(fitH, fitW);
}

float Camera::nearestVisibleDepth(const std::vector<QVector3D> &polygon) const {
    const QVector3D e = eye(), f = forward();
    // Keeps the part of `poly` where dot(n, p - e) >= offset.
    auto clip = [&](std::vector<QVector3D> poly, const QVector3D &n, float offset) {
        std::vector<QVector3D> out;
        for(size_t i = 0; i < poly.size(); ++i) {
            const QVector3D &a = poly[i], &b = poly[(i + 1) % poly.size()];
            const float da = QVector3D::dotProduct(n, a - e) - offset, db = QVector3D::dotProduct(n, b - e) - offset;
            if(da >= 0) out.push_back(a);
            if((da >= 0) != (db >= 0)) out.push_back(a + (b - a) * (da / (da - db)));
        }
        return out;
    };
    std::vector<QVector3D> poly = clip(polygon, f, distance * 1e-4f);
    // The four sides of the view: planes through the eye and two corner rays.
    const float w = float(std::max(1, viewport.width())), h = float(std::max(1, viewport.height()));
    QVector3D rays[4], o;
    const QPointF corners[4] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    for(int k = 0; k < 4; ++k) ray(corners[k], o, rays[k]);
    for(int k = 0; k < 4 && !poly.empty(); ++k) {
        QVector3D n = QVector3D::crossProduct(rays[k], rays[(k + 1) % 4]);
        if(QVector3D::dotProduct(n, f) < 0) n = -n;
        poly = clip(poly, n, 0.0f);
    }
    float nearest = std::numeric_limits<float>::infinity();
    for(const QVector3D &p : poly) nearest = std::min(nearest, QVector3D::dotProduct(p - e, f));
    return nearest;
}

void Camera::updateClipPlanes(const Box3 &solid, const std::vector<QVector3D> &grid) {
    const QVector3D e = eye(), f = forward();
    float nearest = distance, farthest = distance;
    auto add = [&](const QVector3D &p) {
        const float d = QVector3D::dotProduct(p - e, f);
        nearest = std::min(nearest, d);
        farthest = std::max(farthest, d);
    };
    if(!solid.isEmpty())
        for(int k = 0; k < 8; ++k)
            add(QVector3D((k & 1) ? solid.max.x() : solid.min.x(), (k & 2) ? solid.max.y() : solid.min.y(),
                          (k & 4) ? solid.max.z() : solid.min.z()));
    for(const QVector3D &p : grid) farthest = std::max(farthest, QVector3D::dotProduct(p - e, f));
    farPlane = std::max(farthest * 1.05f + 1.0f, distance * 2.0f);
    if(orthographic) {
        nearPlane = -farPlane;
        return;
    }
    if(grid.size() >= 3) nearest = std::min(nearest, nearestVisibleDepth(grid));
    // As far out as possible for depth precision: just in front of what is in
    // view, but never beyond half-way to the orbit target, and not so close
    // that the far / near ratio (the precision) gets out of hand when zoomed
    // right in.
    nearPlane = std::max({nearest * 0.9f, distance * 0.01f, farPlane / 100000.0f, 1e-3f});
    nearPlane = std::min(nearPlane, std::max(distance * 0.5f, farPlane / 100000.0f));
}

} // namespace cadly
