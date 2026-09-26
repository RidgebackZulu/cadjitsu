#include "model/PlaneDisplay.h"

#include "topo/Resolver.h"

#include <cmath>

namespace cadly {

namespace {

QVector3D toQ(const gp_XYZ &p) { return QVector3D(float(p.X()), float(p.Y()), float(p.Z())); }

} // namespace

Quad constructionPlaneQuad(const cad::PlaneResult &plane) {
    const QVector3D c = toQ(plane.center.XYZ());
    const float h = float(plane.halfSize);
    const QVector3D x = toQ(plane.frame.XDirection().XYZ()) * h, y = toQ(plane.frame.YDirection().XYZ()) * h;
    return {c - x - y, c + x - y, c + x + y, c - x + y};
}

Quad originPlaneQuad(cad::PlaneRef::Kind kind, float size) {
    const gp_Ax3 f = cad::originPlaneFrame(kind);
    const QVector3D o = toQ(f.Location().XYZ());
    const QVector3D x = toQ(f.XDirection().XYZ()) * size, y = toQ(f.YDirection().XYZ()) * size;
    return {o, o + x, o + x + y, o + y};
}

std::optional<float> rayQuad(const QVector3D &origin, const QVector3D &direction, const Quad &q, bool orthographic) {
    const QVector3D n = QVector3D::crossProduct(q[1] - q[0], q[3] - q[0]).normalized();
    const float den = QVector3D::dotProduct(direction, n);
    if(std::fabs(den) < 1e-7f) return std::nullopt;
    const float t = QVector3D::dotProduct(q[0] - origin, n) / den;
    if(t < 0 && !orthographic) return std::nullopt;
    const QVector3D p = origin + direction * t;
    // Inside when on the same side of every edge.
    for(int i = 0; i < 4; ++i) {
        const QVector3D e = q[size_t((i + 1) % 4)] - q[size_t(i)];
        if(QVector3D::dotProduct(QVector3D::crossProduct(e, p - q[size_t(i)]), n) < 0) return std::nullopt;
    }
    return t;
}

} // namespace cadly
