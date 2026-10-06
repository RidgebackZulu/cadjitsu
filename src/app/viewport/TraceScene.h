#pragma once

#include "viewport/Camera.h"
#include "viewport/RenderScene.h"

#include "render/PathTracer.h"

#include <QImage>

#include <memory>
#include <unordered_map>

namespace cadjitsu {

// What the canvas shows, for the path tracer: its bodies with their
// materials, and the render settings. Meshes are converted once and kept in
// `cache` (when given) for as long as they are shown.
using TraceMeshCache = std::unordered_map<const cad::MeshData *, std::shared_ptr<cad::rt::TraceMesh>>;
cad::rt::TraceScene traceSceneFor(const RenderScene &scene, TraceMeshCache *cache = nullptr);

// The camera's view as an image of `size` pixels (its aspect follows `size`).
cad::rt::TraceCamera traceCameraFor(Camera camera, const QSize &size);

// A finished, path-traced picture of the scene over the canvas's backdrop
// (in this thread: it takes a while).
struct TracedPicture {
    QImage image;
    int samples = 0;
    bool denoised = false;
};
TracedPicture renderPicture(const RenderScene &scene, const Camera &camera, const QSize &size, int samples,
                            const QColor &backdropTop, const QColor &backdropBottom);

} // namespace cadjitsu
