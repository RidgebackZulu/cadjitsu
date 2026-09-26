#pragma once

#include "viewport/Camera.h"

#include "base/Ids.h"
#include "mesh/MeshData.h"

#include <QPointF>
#include <QRectF>
#include <QVector3D>
#include <QVector4D>

#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace cadly {

// A body as seen by the picker.
struct PickTarget {
    cad::BodyId body;
    std::shared_ptr<const cad::MeshData> mesh;
};

struct PickHit {
    enum class Kind { None, Face, Edge, Vertex };
    Kind kind = Kind::None;
    cad::BodyId body;
    int index = 0;                 // 1-based face / edge / vertex index
    QVector3D point;               // world position of the hit
    float rayT = std::numeric_limits<float>::infinity();
    float screenDistance = 0.0f;   // pixels from the cursor (edges / vertices)

    bool valid() const { return kind != Kind::None; }
    bool sameEntity(const PickHit &o) const { return kind == o.kind && body == o.body && index == o.index; }
};

struct PickOptions {
    bool faces = true;
    bool edges = true;
    bool vertices = true;
    float edgeTolerance = 6.0f;    // pixels
    float vertexTolerance = 8.0f;  // pixels
    std::optional<QVector4D> clipPlane; // keep points with dot(n, p) + d <= 0
};

// CPU picking against display meshes: nearest visible vertex, else edge, else face.
PickHit pick(const Camera &camera, QPointF px, const std::vector<PickTarget> &targets, const PickOptions &options);

// First face hit along the cursor ray (used for orbit pivots and zoom anchors).
std::optional<QVector3D> raycast(const Camera &camera, QPointF px, const std::vector<PickTarget> &targets,
                                 const std::optional<QVector4D> &clipPlane = std::nullopt);

// Bodies fully inside `rect` (window) or touching it (crossing), in pixels.
std::vector<cad::BodyId> bodiesInRect(const Camera &camera, const QRectF &rect, bool crossing,
                                      const std::vector<PickTarget> &targets);

Box3 meshBounds(const cad::MeshData &mesh);

} // namespace cadly
