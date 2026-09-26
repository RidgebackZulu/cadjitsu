#include "viewport/ViewCube.h"

#include <QFont>
#include <QPainter>

#include <array>
#include <cmath>

namespace cadly {

namespace {

constexpr float kProjExtent = 1.85f; // half size of the ortho view around the unit cube

struct FaceDef {
    QVector3D normal, uAxis, vAxis; // v points "up" on the label
    const char *label;
};

// Order matches the atlas tiles.
const std::array<FaceDef, 6> kFaces = {{
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, "TOP"},
    {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}, "BOTTOM"},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, "FRONT"},
    {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, "BACK"},
    {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "RIGHT"},
    {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, "LEFT"},
}};

} // namespace

QRect ViewCube::rect(QSize viewport) {
    return QRect(viewport.width() - kSize - kMargin, kMargin, kSize, kSize);
}

QMatrix4x4 ViewCube::viewRotation(const QQuaternion &cameraRotation) {
    QMatrix4x4 m;
    m.rotate(cameraRotation.inverted());
    return m;
}

QMatrix4x4 ViewCube::viewProjection(const QQuaternion &cameraRotation) {
    QMatrix4x4 proj;
    proj.ortho(-kProjExtent, kProjExtent, -kProjExtent, kProjExtent, -10.0f, 10.0f);
    return proj * viewRotation(cameraRotation);
}

std::optional<QVector3D> ViewCube::hitTest(QPointF px, QSize viewport, const QQuaternion &cameraRotation) {
    const QRect r = rect(viewport);
    if(!r.contains(px.toPoint())) return std::nullopt;
    const float nx = float((px.x() - r.left()) / r.width()) * 2.0f - 1.0f;
    const float ny = 1.0f - float((px.y() - r.top()) / r.height()) * 2.0f;
    // Ray in cube space: from in front of the cube along the camera's forward axis.
    const QVector3D origin = cameraRotation.rotatedVector(QVector3D(nx * kProjExtent, ny * kProjExtent, 5.0f));
    const QVector3D dir = cameraRotation.rotatedVector(QVector3D(0, 0, -1));
    float tmin = -1e9f, tmax = 1e9f;
    for(int k = 0; k < 3; ++k) {
        if(std::fabs(dir[k]) < 1e-9f) {
            if(std::fabs(origin[k]) > 1.0f) return std::nullopt;
            continue;
        }
        float t1 = (-1.0f - origin[k]) / dir[k], t2 = (1.0f - origin[k]) / dir[k];
        if(t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
    }
    if(tmin > tmax) return std::nullopt;
    const QVector3D p = origin + dir * tmin;
    QVector3D region;
    for(int k = 0; k < 3; ++k) region[k] = std::fabs(p[k]) > 0.62f ? (p[k] > 0 ? 1.0f : -1.0f) : 0.0f;
    if(region.isNull()) return std::nullopt;
    return region;
}

QQuaternion ViewCube::orientationForRegion(const QVector3D &dir) {
    const QVector3D viewDir = (-dir).normalized();
    QVector3D up(0, 0, 1);
    if(std::fabs(dir.x()) < 0.5f && std::fabs(dir.y()) < 0.5f) up = dir.z() > 0 ? QVector3D(0, 1, 0) : QVector3D(0, -1, 0);
    const QVector3D z = -viewDir;
    const QVector3D x = QVector3D::crossProduct(up, z).normalized();
    const QVector3D y = QVector3D::crossProduct(z, x);
    return QQuaternion::fromAxes(x, y, z).normalized();
}

void ViewCube::mesh(std::vector<float> &vertices, std::vector<uint16_t> &indices) {
    vertices.clear();
    indices.clear();
    // Each face is a 4x4 grid of quads so the region highlight has sharp edges.
    const int n = 4;
    for(size_t f = 0; f < kFaces.size(); ++f) {
        const FaceDef &fd = kFaces[f];
        const uint16_t base = uint16_t(vertices.size() / 8);
        for(int j = 0; j <= n; ++j) {
            for(int i = 0; i <= n; ++i) {
                const float a = -1.0f + 2.0f * i / n, b = -1.0f + 2.0f * j / n;
                const QVector3D p = fd.normal + fd.uAxis * a + fd.vAxis * b;
                const float u = (float(f) + (a + 1.0f) * 0.5f) / 6.0f;
                const float v = 1.0f - (b + 1.0f) * 0.5f;
                vertices.insert(vertices.end(), {p.x(), p.y(), p.z(), fd.normal.x(), fd.normal.y(), fd.normal.z(), u, v});
            }
        }
        for(int j = 0; j < n; ++j) {
            for(int i = 0; i < n; ++i) {
                const uint16_t a = uint16_t(base + j * (n + 1) + i), b = uint16_t(a + 1);
                const uint16_t c = uint16_t(a + (n + 1)), d = uint16_t(c + 1);
                indices.insert(indices.end(), {a, b, d, a, d, c});
            }
        }
    }
}

QImage ViewCube::labelAtlas(qreal dpr) {
    const int tile = int(128 * dpr);
    QImage img(tile * 6, tile, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font;
    font.setBold(true);
    font.setPixelSize(int(tile * 0.19));
    p.setFont(font);
    for(size_t f = 0; f < kFaces.size(); ++f) {
        const QRect r(int(f) * tile, 0, tile, tile);
        // Soft border that reads as the cube's edges.
        p.setPen(QPen(QColor(120, 128, 140, 200), tile * 0.03));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(1, 1, -1, -1));
        p.setPen(QColor(70, 76, 86));
        p.drawText(r, Qt::AlignCenter, QString::fromLatin1(kFaces[f].label));
    }
    p.end();
    return img;
}

} // namespace cadly
