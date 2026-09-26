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
// FRONT is -Y, RIGHT is +X, TOP is +Z. A chamfered cube: clicking a face,
// bevelled edge or corner looks at the model from that direction. Under it
// sit a compass ring (north is +Y) and an X/Y/Z triad.
class ViewCube {
public:
    static constexpr int kSize = 120;   // logical pixels
    static constexpr int kMargin = 12;
    // Vertex layout: position xyz, normal xyz, uv, region xyz, material.
    static constexpr int kFloatsPerVertex = 12;

    // Region of the cube in logical viewport coordinates.
    static QRect rect(QSize viewport);
    static QPointF centre(QSize viewport);
    // Screen pixels per cube unit (the cube spans -1..1).
    static double pixelsPerUnit();

    // Direction (components in {-1, 0, 1}) of the region under `px`, if any.
    static std::optional<QVector3D> hitTest(QPointF px, QSize viewport, const QQuaternion &cameraRotation);

    // Camera orientation for looking at the model from region `dir`.
    static QQuaternion orientationForRegion(const QVector3D &dir);

    // Model-view-projection for drawing the cube into its viewport rectangle
    // (combine with QRhi::clipSpaceCorrMatrix()).
    static QMatrix4x4 viewProjection(const QQuaternion &cameraRotation);
    static QMatrix4x4 viewRotation(const QQuaternion &cameraRotation);

    // Interleaved vertices (kFloatsPerVertex each) and triangle indices.
    static void mesh(std::vector<float> &vertices, std::vector<uint16_t> &indices);
    static QImage labelAtlas(qreal devicePixelRatio = 4.0);
};

} // namespace cadly
