#include "viewport/TraceScene.h"

#include <QLinearGradient>
#include <QPainter>

#include <cstring>

namespace cadjitsu {

cad::rt::TraceScene traceSceneFor(const RenderScene &scene, TraceMeshCache *cache) {
    cad::rt::TraceScene ts;
    ts.settings = scene.render;
    TraceMeshCache used;
    for(const RenderBody &b : scene.bodies) {
        if(!b.mesh || b.mesh->indices.empty() || b.opacity < 0.999f) continue;
        std::shared_ptr<cad::rt::TraceMesh> tm;
        if(cache) {
            auto it = cache->find(b.mesh.get());
            if(it != cache->end()) tm = it->second;
        }
        if(!tm) {
            tm = std::make_shared<cad::rt::TraceMesh>();
            tm->positions = b.mesh->positions;
            tm->normals = b.mesh->normals;
            tm->indices = b.mesh->indices;
        }
        // The material can change without the mesh: a copy when it differs.
        cad::Optics o = b.optics;
        if(!b.hasOptics) {
            cad::BodyMaterial m;
            m.rgb = uint32_t(b.color.rgb() & 0xffffff);
            o = cad::opticsFor(m);
        }
        const bool translucent = b.hasOptics && b.translucent;
        if(std::memcmp(&tm->optics, &o, sizeof o) != 0 || tm->translucent != translucent) {
            auto copy = std::make_shared<cad::rt::TraceMesh>(*tm);
            copy->optics = o;
            copy->translucent = translucent;
            tm = copy;
        }
        used[b.mesh.get()] = tm;
        ts.meshes.push_back(tm);
    }
    if(cache) *cache = std::move(used);
    return ts;
}

cad::rt::TraceCamera traceCameraFor(Camera camera, const QSize &size) {
    camera.viewport = size;
    cad::rt::TraceCamera c;
    const QMatrix4x4 inv = camera.viewProjection().inverted();
    std::memcpy(c.invViewProj.data(), inv.constData(), 16 * sizeof(float));
    c.width = size.width();
    c.height = size.height();
    return c;
}

TracedPicture renderPicture(const RenderScene &scene, const Camera &camera, const QSize &size, int samples,
                            const QColor &backdropTop, const QColor &backdropBottom) {
    const cad::rt::TraceImage img =
        cad::rt::PathTracer::renderNow(traceSceneFor(scene), traceCameraFor(camera, size), samples, true);
    TracedPicture out;
    out.samples = img.samples;
    out.denoised = img.denoised;
    out.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out.image);
    QLinearGradient g(0, 0, 0, size.height());
    g.setColorAt(0, backdropTop);
    g.setColorAt(1, backdropBottom);
    p.fillRect(out.image.rect(), g);
    p.drawImage(0, 0, QImage(img.rgba.data(), img.width, img.height, QImage::Format_RGBA8888_Premultiplied));
    p.end();
    out.image = out.image.convertToFormat(QImage::Format_RGB32);
    return out;
}

} // namespace cadjitsu
