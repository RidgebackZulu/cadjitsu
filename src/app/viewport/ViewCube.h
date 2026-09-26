#pragma once

#include <QImage>
#include <QMatrix4x4>
#include <QPointF>
#include <QQuaternion>
#include <QRect>
#include <QSize>
#include <QVector3D>

#include <cstdint>
#include <optional>
#include <vector>

namespace cadly {

// The navigation cube in the top-right corner of the viewport (Z up):
// FRONT is -Y, RIGHT is +X, TOP is +Z. Clicking a face, edge or corner
// looks at the model from that direction.
class ViewCube {
public:
    static constexpr int kSize = 104;   // logical pixels
    static constexpr int kMargin = 12;

    // Region of the cube in logical viewport coordinates.
    static QRect rect(QSize viewport);

    // Direction (components in {-1, 0, 1}) of the region under `px`, if any.
    static std::optional<QVector3D> hitTest(QPointF px, QSize viewport, const QQuaternion &cameraRotation);

    // Camera orientation for looking at the model from region `dir`.
    static QQuaternion orientationForRegion(const QVector3D &dir);

    // Model-view-projection for drawing the cube into its viewport rectangle
    // (combine with QRhi::clipSpaceCorrMatrix()).
    static QMatrix4x4 viewProjection(const QQuaternion &cameraRotation);
    static QMatrix4x4 viewRotation(const QQuaternion &cameraRotation);

    // Interleaved vertices (position xyz, normal xyz, uv) and triangle indices.
    static void mesh(std::vector<float> &vertices, std::vector<uint16_t> &indices);
    static QImage labelAtlas(qreal devicePixelRatio = 2.0);
};

} // namespace cadly
