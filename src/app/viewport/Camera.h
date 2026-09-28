#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QQuaternion>
#include <QSize>
#include <QVector3D>

#include <vector>

namespace cadjitsu {

struct Box3 {
    QVector3D min{1e30f, 1e30f, 1e30f};
    QVector3D max{-1e30f, -1e30f, -1e30f};

    bool isEmpty() const { return min.x() > max.x(); }
    void add(const QVector3D &p) {
        min = QVector3D(std::min(min.x(), p.x()), std::min(min.y(), p.y()), std::min(min.z(), p.z()));
        max = QVector3D(std::max(max.x(), p.x()), std::max(max.y(), p.y()), std::max(max.z(), p.z()));
    }
    void add(const Box3 &b) {
        if(b.isEmpty()) return;
        add(b.min);
        add(b.max);
    }
    QVector3D center() const { return (min + max) * 0.5f; }
    float radius() const { return isEmpty() ? 0.0f : (max - min).length() * 0.5f; }
};

// Standard view directions of a Z-up world, as seen on the ViewCube.
enum class StandardView { Front, Back, Left, Right, Top, Bottom, Home };

// An orbiting CAD camera. Z is up. The camera looks at `target` from
// `distance` away; `rotation` maps camera space (looking down -Z, Y up) to world.
class Camera {
public:
    QVector3D target{0, 0, 0};
    float distance = 300.0f;
    QQuaternion rotation;
    float fovY = 30.0f;          // degrees (perspective)
    bool orthographic = false;
    QSize viewport{800, 600};    // logical pixels
    float nearPlane = 0.1f, farPlane = 10000.0f;

    Camera();

    QVector3D eye() const { return target - forward() * distance; }
    QVector3D forward() const { return rotation.rotatedVector(QVector3D(0, 0, -1)); }
    QVector3D up() const { return rotation.rotatedVector(QVector3D(0, 1, 0)); }
    QVector3D right() const { return rotation.rotatedVector(QVector3D(1, 0, 0)); }

    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix() const; // OpenGL convention; combine with QRhi::clipSpaceCorrMatrix()
    QMatrix4x4 viewProjection() const { return projectionMatrix() * viewMatrix(); }
    // Height of the visible area at the target, in world units (drives ortho size).
    float viewHeightAtTarget() const;

    // Picking ray through a logical pixel position (origin on the near plane).
    void ray(QPointF px, QVector3D &origin, QVector3D &dir) const;
    // Projects a world point to logical pixels; `depth` receives the view depth.
    QPointF project(const QVector3D &world, float *depth = nullptr) const;
    // World units per logical pixel at a given view depth.
    float unitsPerPixel(float depth) const;
    float depthOf(const QVector3D &world) const { return QVector3D::dotProduct(world - eye(), forward()); }

    // Fusion-style navigation.
    void orbit(float dxPixels, float dyPixels, const QVector3D &pivot); // turntable about world Z + screen X
    void pan(QPointF deltaPixels, const QVector3D &anchor);            // anchor moves with the cursor
    void zoom(float factor, const QVector3D &anchor);                  // factor > 1 zooms in, anchor stays put

    void setView(StandardView v);
    void setOrientation(const QVector3D &viewDir, const QVector3D &upHint);
    // Frames the box without changing the orientation.
    void fit(const Box3 &box, float margin = 1.15f);
    // Sets the near plane just in front of `solid` (the model and what is drawn
    // with it) and of the part of the `grid` square in view, and the far plane
    // behind both. The near plane is what sets the depth precision, so only
    // the visible part of the grid (much bigger than the model, and reaching
    // under the camera) counts.
    void updateClipPlanes(const Box3 &solid, const std::vector<QVector3D> &grid = {});
    // The nearest depth of the part of a flat convex polygon (such as the grid)
    // in view, or +infinity if none of it is. Perspective only.
    float nearestVisibleDepth(const std::vector<QVector3D> &polygon) const;

    static QQuaternion orientationFor(StandardView v);
};

} // namespace cadjitsu
