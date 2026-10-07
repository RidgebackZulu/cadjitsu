#pragma once

#include "mesh/MeshData.h"

#include "render/Materials.h"

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QRectF>
#include <QVector3D>
#include <QVector4D>

#include <memory>
#include <optional>
#include <vector>

namespace cadjitsu {

// How bodies are drawn (Fusion's "Visual Style").
enum class DisplayStyle { ShadedWithEdges, Shaded, Wireframe, Rendered };

struct RenderBody {
    std::shared_ptr<const cad::MeshData> mesh;
    QColor color;
    float opacity = 1.0f;
    bool edges = true;
    // What it is printed in (the Rendered style's materials).
    cad::Optics optics;
    bool hasOptics = false; // without: matte PLA in `color`
    bool translucent = false;
};

struct FaceHighlight {
    std::shared_ptr<const cad::MeshData> mesh;
    int face = 0; // 1-based
    QColor color;
};

struct EdgeHighlight {
    std::shared_ptr<const cad::MeshData> mesh;
    int edge = 0; // 1-based
    QColor color;
    float width = 3.0f;
};

// Arbitrary geometry drawn every frame (sketches, axes, manipulators...).
struct LineBatch {
    std::vector<QVector3D> segments; // pairs of points
    QColor color;
    float width = 1.5f;
    bool depthTest = true;
    bool ignoreClip = false;
};

struct PointBatch {
    std::vector<QVector3D> points;
    QColor color;
    QColor outline;
    float size = 7.0f;
    bool round = true;
    bool depthTest = true;
};

struct TriangleBatch {
    std::vector<QVector3D> triangles; // triples of points
    QColor color;                     // alpha < 1 = transparent
    bool depthTest = true;
    bool twoSided = true;
};

// A reference picture on a plane (a canvas): drawn see-through, behind
// sketch geometry, hidden by bodies in front of it.
struct CanvasQuad {
    QImage image;          // any format; cached on the GPU by its cacheKey()
    QVector3D corners[4];  // world positions of the picture's top-left, top-right, bottom-right, bottom-left
    float opacity = 0.5f;
};

struct RenderScene {
    DisplayStyle style = DisplayStyle::ShadedWithEdges;
    bool grid = true;
    QMatrix4x4 gridFrame; // grid plane placement (the XY plane by default; the sketch plane while sketching)
    float gridExtent = 500.0f;
    float gridMinor = 10.0f;
    float gridMajor = 100.0f;

    std::vector<RenderBody> bodies;
    std::vector<FaceHighlight> faceHighlights;
    std::vector<EdgeHighlight> edgeHighlights;
    std::vector<PointBatch> points;
    std::vector<LineBatch> lines;
    std::vector<TriangleBatch> triangles;
    std::vector<CanvasQuad> canvases;

    std::optional<QVector4D> clipPlane; // section analysis: dot(n, p) + d > 0 is removed
    // Section analysis: a square on the clip plane (4 corners) that closes the
    // cut of every opaque body, hatched in the body's colour. Empty: no caps.
    std::vector<QVector3D> capQuad;

    bool viewCube = true;
    std::optional<QVector3D> viewCubeHover;

    // The Rendered style's set: build plate, lighting, print surface.
    cad::RenderSettings render;
    // Refine the Rendered style with the path tracer when the view rests.
    bool rayTrace = false;
    // The path tracer's image (premultiplied, framebuffer-sized or smaller),
    // drawn over the bodies and under highlights. Null: none.
    QImage traced;
};

} // namespace cadjitsu
