#pragma once

#include "viewport/Camera.h"
#include "viewport/RenderScene.h"

#include <QColor>

#include <memory>
#include <unordered_map>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

namespace cadly {

// Draws a RenderScene with Qt RHI (Metal / OpenGL / Vulkan / D3D).
class Renderer {
public:
    Renderer();
    ~Renderer();

    void initialize(QRhi *rhi, QRhiRenderPassDescriptor *rp, int sampleCount);
    void releaseResources();
    bool isInitialized() const { return m_rhi != nullptr; }

    // `dpr` converts logical pixels (camera viewport, line widths) to framebuffer pixels.
    void render(QRhiCommandBuffer *cb, QRhiRenderTarget *rt, const RenderScene &scene, const Camera &camera, float dpr);

    QColor backgroundTop{250, 251, 252};
    QColor backgroundBottom{196, 205, 216};

    size_t cachedMeshCount() const { return m_meshes.size(); }

private:
    struct GpuMesh;
    struct DrawCall;
    struct Pipelines;

    GpuMesh &meshFor(const std::shared_ptr<const cad::MeshData> &mesh, QRhiResourceUpdateBatch *u);
    void createPipelines();
    void ensureDynamicBuffer(std::unique_ptr<QRhiBuffer> &buf, quint32 size, int usage);
    void evictMeshes();

    QRhi *m_rhi = nullptr;
    QRhiRenderPassDescriptor *m_rp = nullptr;
    int m_sampleCount = 1;
    uint64_t m_frame = 0;

    std::unique_ptr<Pipelines> m_p;
    std::unordered_map<const cad::MeshData *, std::unique_ptr<GpuMesh>> m_meshes;
};

} // namespace cadly
