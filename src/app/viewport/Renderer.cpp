#include "viewport/Renderer.h"

#include "viewport/ShaderLoader.h"
#include "viewport/ViewCube.h"

#include <rhi/qrhi.h>

#include <cmath>
#include <cstring>

namespace cadly {

namespace {

struct FrameUniforms {
    float viewProj[16];
    float eyePos[4];
    float eyeDir[4];
    float lightDir[4];
    float viewport[4];
    float clipPlane[4];
    float misc[4];
};

struct DrawUniforms {
    float model[16];
    float color[4];
    float color2[4];
    float params[4]; // x = width / size (px), y = depth bias, z = lit / round, w = ignore clip
};

struct CubeUniforms {
    float mvp[16];
    float rotation[16];
    float highlight[4];
};

struct BackgroundUniforms {
    float top[4];
    float bottom[4];
    float ndcYUp;
    float pad[3];
};

void setMat(float out[16], const QMatrix4x4 &m) { std::memcpy(out, m.constData(), 16 * sizeof(float)); }
void setVec(float out[4], float x, float y, float z, float w) {
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}
void setVec(float out[4], const QVector3D &v, float w) { setVec(out, v.x(), v.y(), v.z(), w); }
void setColor(float out[4], const QColor &c, float alpha = -1.0f) {
    setVec(out, float(c.redF()), float(c.greenF()), float(c.blueF()), alpha >= 0 ? alpha : float(c.alphaF()));
}

enum Blend { NoBlend, AlphaBlend, Premultiplied };

// Fractions of the eye distance by which edges / overlays move towards the camera.
constexpr float kEdgeBias = 0.0012f;
constexpr float kOverlayBias = 0.0004f;

} // namespace

struct Renderer::GpuMesh {
    std::shared_ptr<const cad::MeshData> data; // keeps the cache key alive
    std::unique_ptr<QRhiBuffer> vbuf, ibuf, edgeBuf;
    quint32 indexCount = 0, segmentCount = 0;
    std::vector<std::pair<quint32, quint32>> edgeSegs; // per edge: first segment, count
    uint64_t lastUsed = 0;
};

struct Renderer::DrawCall {
    QRhiGraphicsPipeline *pipeline = nullptr;
    QRhiShaderResourceBindings *srb = nullptr; // the common one if null
    quint32 uniform = 0;
    QRhiBuffer *vb0 = nullptr;
    quint32 vb0Offset = 0;
    QRhiBuffer *vb1 = nullptr;
    quint32 vb1Offset = 0;
    QRhiBuffer *ib = nullptr;
    quint32 count = 0;
    quint32 instances = 1;
    quint32 firstIndex = 0;
};

struct Renderer::Pipelines {
    std::unique_ptr<QRhiBuffer> frameUbo, drawUbo, bgUbo, cubeUbo;
    quint32 drawStride = 0, drawCapacity = 0;
    std::unique_ptr<QRhiShaderResourceBindings> srb, bgSrb, cubeSrb;
    std::unique_ptr<QRhiBuffer> lineCorners, quadCorners, cubeVbuf, cubeIbuf;
    quint32 cubeIndexCount = 0;
    std::unique_ptr<QRhiTexture> cubeTex;
    std::unique_ptr<QRhiSampler> cubeSampler;
    std::unique_ptr<QRhiBuffer> dynLines, dynPoints, dynTris, capQuad;
    // Rendered style: the model seen from above (white where it is) for the
    // ground's contact shadow.
    std::unique_ptr<QRhiTexture> silTex;
    std::unique_ptr<QRhiTextureRenderTarget> silRt;
    std::unique_ptr<QRhiRenderPassDescriptor> silRp;
    std::unique_ptr<QRhiBuffer> silUbo;
    std::unique_ptr<QRhiSampler> silSampler;
    std::unique_ptr<QRhiShaderResourceBindings> silSrb, groundSrb;
    QRhiResourceUpdateBatch *pending = nullptr;

    std::unique_ptr<QRhiGraphicsPipeline> bg, mesh, meshOverlay, meshOverlayNoDepth, line, lineNoDepth, point,
        pointNoDepth, grid, cube, stencilParity, cap, silhouette, ground;
};

Renderer::Renderer() = default;
Renderer::~Renderer() { releaseResources(); }

void Renderer::releaseResources() {
    if(m_p && m_p->pending) m_p->pending->release();
    m_meshes.clear();
    m_p.reset();
    m_rhi = nullptr;
    m_rp = nullptr;
}

void Renderer::initialize(QRhi *rhi, QRhiRenderPassDescriptor *rp, int sampleCount) {
    if(m_rhi == rhi && m_rp == rp && m_sampleCount == sampleCount && m_p) return;
    releaseResources();
    m_rhi = rhi;
    m_rp = rp;
    m_sampleCount = sampleCount;
    m_p = std::make_unique<Pipelines>();
    createPipelines();
}

void Renderer::ensureDynamicBuffer(std::unique_ptr<QRhiBuffer> &buf, quint32 size, int usage) {
    size = std::max<quint32>(size, 256);
    if(buf && buf->size() >= size) return;
    quint32 cap = 1024;
    while(cap < size) cap *= 2;
    buf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UsageFlags(usage), cap));
    buf->create();
}

// Frame + per-draw uniforms for everything, the top-down frame for the
// silhouette pass, and the silhouette image for the ground.
void Renderer::rebuildBindings() {
    Pipelines &p = *m_p;
    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    const auto draw = QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(1, stages, p.drawUbo.get(), sizeof(DrawUniforms));
    p.srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, p.frameUbo.get()), draw});
    p.srb->create();
    p.silSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, p.silUbo.get()), draw});
    p.silSrb->create();
    p.groundSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, p.frameUbo.get()), draw,
                              QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                                        p.silTex.get(), p.silSampler.get())});
    p.groundSrb->create();
}

void Renderer::createPipelines() {
    Pipelines &p = *m_p;
    QRhi *rhi = m_rhi;
    p.pending = rhi->nextResourceUpdateBatch();

    p.frameUbo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FrameUniforms)));
    p.frameUbo->create();
    p.drawStride = quint32(rhi->ubufAligned(sizeof(DrawUniforms)));
    p.drawCapacity = 256;
    p.drawUbo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, p.drawStride * p.drawCapacity));
    p.drawUbo->create();

    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    // The rendered style's ground shadow: an offscreen top-down coverage image.
    p.silTex.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(512, 512), 1,
                                   QRhiTexture::RenderTarget | QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips));
    p.silTex->create();
    p.silRt.reset(rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(QRhiColorAttachment(p.silTex.get()))));
    p.silRp.reset(p.silRt->newCompatibleRenderPassDescriptor());
    p.silRt->setRenderPassDescriptor(p.silRp.get());
    p.silRt->create();
    p.silUbo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FrameUniforms)));
    p.silUbo->create();
    p.silSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                       QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    p.silSampler->create();
    p.srb.reset(rhi->newShaderResourceBindings());
    p.silSrb.reset(rhi->newShaderResourceBindings());
    p.groundSrb.reset(rhi->newShaderResourceBindings());
    rebuildBindings();

    p.bgUbo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(BackgroundUniforms)));
    p.bgUbo->create();
    p.bgSrb.reset(rhi->newShaderResourceBindings());
    p.bgSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, p.bgUbo.get())});
    p.bgSrb->create();

    // Static corner buffers.
    static const float lineCorners[] = {0, -1, 1, -1, 1, 1, 0, -1, 1, 1, 0, 1};
    static const float quadCorners[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    p.lineCorners.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof lineCorners));
    p.lineCorners->create();
    p.pending->uploadStaticBuffer(p.lineCorners.get(), lineCorners);
    p.quadCorners.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof quadCorners));
    p.quadCorners->create();
    p.pending->uploadStaticBuffer(p.quadCorners.get(), quadCorners);

    // ViewCube geometry and labels.
    std::vector<float> cv;
    std::vector<uint16_t> ci;
    ViewCube::mesh(cv, ci);
    p.cubeVbuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, quint32(cv.size() * 4)));
    p.cubeVbuf->create();
    p.pending->uploadStaticBuffer(p.cubeVbuf.get(), cv.data());
    p.cubeIbuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(ci.size() * 2)));
    p.cubeIbuf->create();
    p.pending->uploadStaticBuffer(p.cubeIbuf.get(), ci.data());
    p.cubeIndexCount = quint32(ci.size());
    const QImage atlas = ViewCube::labelAtlas(2.0);
    p.cubeTex.reset(rhi->newTexture(QRhiTexture::RGBA8, atlas.size()));
    p.cubeTex->create();
    p.pending->uploadTexture(p.cubeTex.get(), atlas);
    p.cubeSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    p.cubeSampler->create();
    p.cubeUbo.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(CubeUniforms)));
    p.cubeUbo->create();
    p.cubeSrb.reset(rhi->newShaderResourceBindings());
    p.cubeSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, p.cubeUbo.get()),
                            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                                      p.cubeTex.get(), p.cubeSampler.get())});
    p.cubeSrb->create();

    auto make = [&](const char *vs, const char *fs, const QRhiVertexInputLayout &layout, bool depthTest,
                    bool depthWrite, Blend blend, QRhiShaderResourceBindings *srb) {
        std::unique_ptr<QRhiGraphicsPipeline> pl(rhi->newGraphicsPipeline());
        pl->setShaderStages({{QRhiShaderStage::Vertex, loadShader(QString::fromLatin1(vs))},
                             {QRhiShaderStage::Fragment, loadShader(QString::fromLatin1(fs))}});
        pl->setVertexInputLayout(layout);
        pl->setDepthTest(depthTest);
        pl->setDepthWrite(depthWrite);
        pl->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if(blend != NoBlend) {
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = blend == Premultiplied ? QRhiGraphicsPipeline::One : QRhiGraphicsPipeline::SrcAlpha;
            b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            b.srcAlpha = QRhiGraphicsPipeline::One;
            b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            pl->setTargetBlends({b});
        }
        if(depthWrite && QString::fromLatin1(vs) == QLatin1String("mesh.vert")) {
            // Push opaque faces back slightly so coplanar edges and overlays draw on top.
            pl->setDepthBias(2);
            pl->setSlopeScaledDepthBias(1.5f);
        }
        pl->setSampleCount(m_sampleCount);
        pl->setShaderResourceBindings(srb);
        pl->setRenderPassDescriptor(m_rp);
        pl->create();
        return pl;
    };

    QRhiVertexInputLayout empty;
    QRhiVertexInputLayout meshLayout;
    meshLayout.setBindings({{6 * sizeof(float)}});
    meshLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                              {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)}});
    QRhiVertexInputLayout lineLayout;
    lineLayout.setBindings({{2 * sizeof(float)}, {6 * sizeof(float), QRhiVertexInputBinding::PerInstance}});
    lineLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                              {1, 1, QRhiVertexInputAttribute::Float3, 0},
                              {1, 2, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)}});
    QRhiVertexInputLayout pointLayout;
    pointLayout.setBindings({{2 * sizeof(float)}, {3 * sizeof(float), QRhiVertexInputBinding::PerInstance}});
    pointLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {1, 1, QRhiVertexInputAttribute::Float3, 0}});
    QRhiVertexInputLayout gridLayout;
    gridLayout.setBindings({{2 * sizeof(float)}});
    gridLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});
    QRhiVertexInputLayout cubeLayout;
    cubeLayout.setBindings({{8 * sizeof(float)}});
    cubeLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                              {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
                              {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)}});

    p.bg = make("background.vert", "background.frag", empty, false, false, NoBlend, p.bgSrb.get());
    p.mesh = make("mesh.vert", "mesh.frag", meshLayout, true, true, NoBlend, p.srb.get());
    p.meshOverlay = make("mesh.vert", "mesh.frag", meshLayout, true, false, AlphaBlend, p.srb.get());
    p.meshOverlayNoDepth = make("mesh.vert", "mesh.frag", meshLayout, false, false, AlphaBlend, p.srb.get());
    p.line = make("line.vert", "line.frag", lineLayout, true, false, AlphaBlend, p.srb.get());
    p.lineNoDepth = make("line.vert", "line.frag", lineLayout, false, false, AlphaBlend, p.srb.get());
    p.point = make("point.vert", "point.frag", pointLayout, true, false, AlphaBlend, p.srb.get());
    p.pointNoDepth = make("point.vert", "point.frag", pointLayout, false, false, AlphaBlend, p.srb.get());
    p.grid = make("grid.vert", "grid.frag", gridLayout, true, false, Premultiplied, p.srb.get());

    // Section caps. The parity pass flips the stencil for every (unclipped)
    // surface of a body along each view ray, without drawing: it ends up set
    // where the ray passes through the cut. The cap then fills those pixels on
    // the clip plane and clears the stencil for the next body.
    auto stencilPipeline = [&](const char *vs, const char *fs, bool colour, QRhiGraphicsPipeline::StencilOpState op) {
        std::unique_ptr<QRhiGraphicsPipeline> pl(rhi->newGraphicsPipeline());
        pl->setShaderStages({{QRhiShaderStage::Vertex, loadShader(QString::fromLatin1(vs))},
                             {QRhiShaderStage::Fragment, loadShader(QString::fromLatin1(fs))}});
        pl->setVertexInputLayout(meshLayout);
        pl->setDepthTest(colour);
        pl->setDepthWrite(colour);
        pl->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        if(!colour) {
            QRhiGraphicsPipeline::TargetBlend none;
            none.colorWrite = {};
            pl->setTargetBlends({none});
        }
        pl->setStencilTest(true);
        pl->setStencilFront(op);
        pl->setStencilBack(op);
        pl->setStencilReadMask(0xFF);
        pl->setStencilWriteMask(0xFF);
        pl->setSampleCount(m_sampleCount);
        pl->setShaderResourceBindings(p.srb.get());
        pl->setRenderPassDescriptor(m_rp);
        pl->create();
        return pl;
    };
    QRhiGraphicsPipeline::StencilOpState flip;
    flip.compareOp = QRhiGraphicsPipeline::Always;
    flip.passOp = QRhiGraphicsPipeline::Invert;
    p.stencilParity = stencilPipeline("mesh.vert", "mesh.frag", false, flip);

    // Rendered style: the silhouette pass and the ground.
    {
        std::unique_ptr<QRhiGraphicsPipeline> pl(rhi->newGraphicsPipeline());
        pl->setShaderStages({{QRhiShaderStage::Vertex, loadShader(QStringLiteral("mesh.vert"))},
                             {QRhiShaderStage::Fragment, loadShader(QStringLiteral("mesh.frag"))}});
        pl->setVertexInputLayout(meshLayout);
        pl->setShaderResourceBindings(p.silSrb.get());
        pl->setRenderPassDescriptor(p.silRp.get());
        pl->create();
        p.silhouette = std::move(pl);
    }
    {
        std::unique_ptr<QRhiGraphicsPipeline> pl(rhi->newGraphicsPipeline());
        pl->setShaderStages({{QRhiShaderStage::Vertex, loadShader(QStringLiteral("ground.vert"))},
                             {QRhiShaderStage::Fragment, loadShader(QStringLiteral("ground.frag"))}});
        pl->setVertexInputLayout(gridLayout);
        pl->setDepthTest(true);
        pl->setDepthWrite(false);
        pl->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true;
        b.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        b.srcAlpha = QRhiGraphicsPipeline::One;
        b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pl->setTargetBlends({b});
        pl->setSampleCount(m_sampleCount);
        pl->setShaderResourceBindings(p.groundSrb.get());
        pl->setRenderPassDescriptor(m_rp);
        pl->create();
        p.ground = std::move(pl);
    }
    QRhiGraphicsPipeline::StencilOpState fill;
    fill.compareOp = QRhiGraphicsPipeline::NotEqual; // against reference 0
    fill.passOp = QRhiGraphicsPipeline::StencilZero;
    fill.depthFailOp = QRhiGraphicsPipeline::StencilZero;
    p.cap = stencilPipeline("cap.vert", "cap.frag", true, fill);
    p.cube = make("cube.vert", "cube.frag", cubeLayout, true, true, NoBlend, p.cubeSrb.get());
}

Renderer::GpuMesh &Renderer::meshFor(const std::shared_ptr<const cad::MeshData> &mesh, QRhiResourceUpdateBatch *u) {
    auto it = m_meshes.find(mesh.get());
    if(it != m_meshes.end()) {
        it->second->lastUsed = m_frame;
        return *it->second;
    }
    auto g = std::make_unique<GpuMesh>();
    g->data = mesh;
    g->lastUsed = m_frame;
    const cad::MeshData &m = *mesh;
    if(m.vertexCount() > 0 && !m.indices.empty()) {
        std::vector<float> vb(m.vertexCount() * 6);
        for(size_t i = 0; i < m.vertexCount(); ++i) {
            for(int k = 0; k < 3; ++k) {
                vb[6 * i + k] = m.positions[3 * i + k];
                vb[6 * i + 3 + k] = m.normals[3 * i + k];
            }
        }
        g->vbuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, quint32(vb.size() * 4)));
        g->vbuf->create();
        u->uploadStaticBuffer(g->vbuf.get(), vb.data());
        g->ibuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(m.indices.size() * 4)));
        g->ibuf->create();
        u->uploadStaticBuffer(g->ibuf.get(), m.indices.data());
        g->indexCount = quint32(m.indices.size());
    }
    std::vector<float> seg;
    g->edgeSegs.resize(m.edgeRanges.size());
    for(size_t e = 0; e < m.edgeRanges.size(); ++e) {
        const auto &r = m.edgeRanges[e];
        const quint32 first = quint32(seg.size() / 6);
        for(uint32_t k = 0; k + 1 < r.count; ++k) {
            for(int j = 0; j < 2; ++j)
                for(int c = 0; c < 3; ++c) seg.push_back(m.edgePoints[3 * (r.first + k + j) + c]);
        }
        g->edgeSegs[e] = {first, quint32(seg.size() / 6) - first};
    }
    if(!seg.empty()) {
        g->edgeBuf.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, quint32(seg.size() * 4)));
        g->edgeBuf->create();
        u->uploadStaticBuffer(g->edgeBuf.get(), seg.data());
        g->segmentCount = quint32(seg.size() / 6);
    }
    GpuMesh &ref = *g;
    m_meshes[mesh.get()] = std::move(g);
    return ref;
}

void Renderer::evictMeshes() {
    for(auto it = m_meshes.begin(); it != m_meshes.end();) {
        if(m_frame - it->second->lastUsed > 240) it = m_meshes.erase(it);
        else ++it;
    }
}

void Renderer::render(QRhiCommandBuffer *cb, QRhiRenderTarget *rt, const RenderScene &scene, const Camera &cam,
                      float dpr) {
    ++m_frame;
    Pipelines &p = *m_p;
    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    if(p.pending) {
        u->merge(p.pending);
        p.pending->release();
        p.pending = nullptr;
    }
    const QSize fb = rt->pixelSize();
    const QMatrix4x4 corr = m_rhi->clipSpaceCorrMatrix();

    FrameUniforms fu{};
    setMat(fu.viewProj, corr * cam.viewProjection());
    setVec(fu.eyePos, cam.eye(), cam.orthographic ? 1.0f : 0.0f);
    setVec(fu.eyeDir, cam.forward(), 0.0f);
    const QVector3D light = (-cam.forward() + cam.up() * 0.8f - cam.right() * 0.45f).normalized();
    setVec(fu.lightDir, light, 0.0f);
    setVec(fu.viewport, float(fb.width()), float(fb.height()), 1.0f / std::max(1, fb.width()),
           1.0f / std::max(1, fb.height()));
    if(scene.clipPlane) {
        setVec(fu.clipPlane, scene.clipPlane->x(), scene.clipPlane->y(), scene.clipPlane->z(), scene.clipPlane->w());
        fu.misc[0] = 1.0f;
    }
    u->updateDynamicBuffer(p.frameUbo.get(), 0, sizeof fu, &fu);

    BackgroundUniforms bu{};
    setColor(bu.top, backgroundTop, 1.0f);
    setColor(bu.bottom, backgroundBottom, 1.0f);
    bu.ndcYUp = m_rhi->isYUpInNDC() ? 1.0f : -1.0f;
    u->updateDynamicBuffer(p.bgUbo.get(), 0, sizeof bu, &bu);

    std::vector<DrawCall> draws;
    std::vector<DrawUniforms> uniforms;
    const QMatrix4x4 identity;
    auto uniform = [&](const QColor &c, const QColor &c2, float x, float y, float z, float w) {
        DrawUniforms du{};
        setMat(du.model, identity);
        setColor(du.color, c);
        setColor(du.color2, c2);
        setVec(du.params, x, y, z, w);
        uniforms.push_back(du);
        return quint32(uniforms.size() - 1);
    };

    const bool faces = scene.style != DisplayStyle::Wireframe;
    const bool rendered = scene.style == DisplayStyle::Rendered;
    const bool edges = scene.style == DisplayStyle::ShadedWithEdges || scene.style == DisplayStyle::Wireframe;
    const QColor edgeColor(28, 33, 40);

    // Opaque bodies.
    std::vector<GpuMesh *> gpu(scene.bodies.size(), nullptr);
    for(size_t i = 0; i < scene.bodies.size(); ++i) {
        const RenderBody &b = scene.bodies[i];
        if(!b.mesh) continue;
        gpu[i] = &meshFor(b.mesh, u);
        if(!faces || b.opacity < 0.999f || !gpu[i]->ibuf) continue;
        DrawCall d;
        d.pipeline = p.mesh.get();
        d.uniform = uniform(b.color, b.color.darker(160), 0, 0, 1, rendered ? 1.0f : 0.0f);
        d.vb0 = gpu[i]->vbuf.get();
        d.ib = gpu[i]->ibuf.get();
        d.count = gpu[i]->indexCount;
        draws.push_back(d);
    }

    // Section caps, per opaque body.
    if(faces && scene.clipPlane && scene.capQuad.size() == 4) {
        const QVector3D *q = scene.capQuad.data();
        const QVector3D n = QVector3D::crossProduct(q[1] - q[0], q[3] - q[0]).normalized();
        std::vector<float> quad;
        for(int k : {0, 1, 2, 0, 2, 3}) quad.insert(quad.end(), {q[k].x(), q[k].y(), q[k].z(), n.x(), n.y(), n.z()});
        ensureDynamicBuffer(p.capQuad, quint32(quad.size() * 4), QRhiBuffer::VertexBuffer);
        u->updateDynamicBuffer(p.capQuad.get(), 0, quint32(quad.size() * 4), quad.data());
        for(size_t i = 0; i < scene.bodies.size(); ++i) {
            const RenderBody &b = scene.bodies[i];
            if(!gpu[i] || !gpu[i]->ibuf || b.opacity < 0.999f) continue;
            DrawCall parity;
            parity.pipeline = p.stencilParity.get();
            parity.uniform = uniform(b.color, b.color, 0, 0, 0, 0);
            parity.vb0 = gpu[i]->vbuf.get();
            parity.ib = gpu[i]->ibuf.get();
            parity.count = gpu[i]->indexCount;
            draws.push_back(parity);
            DrawCall cap;
            cap.pipeline = p.cap.get();
            cap.uniform = uniform(b.color.darker(112), b.color.darker(190), 7.0f * dpr, std::max(1.0f, 1.2f * dpr), 0, 0);
            cap.vb0 = p.capQuad.get();
            cap.count = 6;
            draws.push_back(cap);
        }
    }

    // Rendered style: a soft contact shadow on the ground under the model, from
    // a top-down silhouette of it (drawn before the main pass).
    std::vector<DrawCall> silDraws;
    if(rendered) {
        QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
        for(size_t i = 0; i < scene.bodies.size(); ++i) {
            const RenderBody &b = scene.bodies[i];
            if(!gpu[i] || !gpu[i]->ibuf || b.opacity < 0.999f) continue;
            lo = QVector3D(std::min(lo.x(), b.mesh->bboxMin[0]), std::min(lo.y(), b.mesh->bboxMin[1]), std::min(lo.z(), b.mesh->bboxMin[2]));
            hi = QVector3D(std::max(hi.x(), b.mesh->bboxMax[0]), std::max(hi.y(), b.mesh->bboxMax[1]), std::max(hi.z(), b.mesh->bboxMax[2]));
            DrawCall d;
            d.pipeline = p.silhouette.get();
            d.srb = p.silSrb.get();
            d.uniform = uniform(Qt::white, Qt::white, 0, 0, 0, 0);
            d.vb0 = gpu[i]->vbuf.get();
            d.ib = gpu[i]->ibuf.get();
            d.count = gpu[i]->indexCount;
            silDraws.push_back(d);
        }
        if(!silDraws.empty()) {
            const float size = (hi - lo).length();
            const float half = std::max(hi.x() - lo.x(), hi.y() - lo.y()) * 0.5f * 1.7f + size * 0.1f + 1.0f;
            const QVector3D c = (lo + hi) * 0.5f;
            QMatrix4x4 view, proj;
            view.lookAt(QVector3D(c.x(), c.y(), hi.z() + 1.0f), QVector3D(c.x(), c.y(), lo.z()), QVector3D(0, 1, 0));
            proj.ortho(-half, half, -half, half, 0.5f, hi.z() - lo.z() + 2.0f);
            const QMatrix4x4 top = corr * proj * view;
            FrameUniforms sf = fu;
            setMat(sf.viewProj, top);
            u->updateDynamicBuffer(p.silUbo.get(), 0, sizeof sf, &sf);
            DrawCall g;
            g.pipeline = p.ground.get();
            g.srb = p.groundSrb.get();
            const bool flipY = m_rhi->isYUpInNDC() && !m_rhi->isYUpInFramebuffer();
            g.uniform = uniform(QColor(18, 24, 34, 165), QColor(flipY ? 255 : 0, 0, 0), c.x(), c.y(), half,
                                lo.z() - 0.002f * size);
            setMat(uniforms.back().model, top);
            setVec(uniforms.back().color2, flipY ? 1.0f : 0.0f, 0.014f, 0.055f, 0.0f);
            g.vb0 = p.quadCorners.get();
            g.count = 6;
            draws.push_back(g);
        }
    }

    // Grid on the XY plane (not in the rendered style: the ground takes its place).
    if(scene.grid && !rendered) {
        DrawCall d;
        d.pipeline = p.grid.get();
        d.uniform = uniform(QColor(120, 132, 148, 70), QColor(96, 108, 124, 130), scene.gridExtent, scene.gridMinor,
                            scene.gridMajor, 0);
        setMat(uniforms.back().model, scene.gridFrame);
        d.vb0 = p.quadCorners.get();
        d.count = 6;
        draws.push_back(d);
    }

    // Body edges.
    for(size_t i = 0; i < scene.bodies.size(); ++i) {
        const RenderBody &b = scene.bodies[i];
        if(!gpu[i] || !gpu[i]->edgeBuf || !(edges && b.edges)) continue;
        DrawCall d;
        d.pipeline = p.line.get();
        const QColor c = scene.style == DisplayStyle::Wireframe ? QColor(40, 48, 60) : edgeColor;
        d.uniform = uniform(c, c, 1.3f * dpr, faces ? kEdgeBias : 0.0f, 0, 0);
        d.vb0 = p.lineCorners.get();
        d.vb1 = gpu[i]->edgeBuf.get();
        d.count = 6;
        d.instances = gpu[i]->segmentCount;
        draws.push_back(d);
    }

    // Transparent bodies.
    for(size_t i = 0; i < scene.bodies.size(); ++i) {
        const RenderBody &b = scene.bodies[i];
        if(!gpu[i] || !gpu[i]->ibuf || !faces || b.opacity >= 0.999f) continue;
        DrawCall d;
        d.pipeline = p.meshOverlay.get();
        QColor c = b.color;
        c.setAlphaF(b.opacity);
        d.uniform = uniform(c, c.darker(140), 0, 0, 1, 0);
        d.vb0 = gpu[i]->vbuf.get();
        d.ib = gpu[i]->ibuf.get();
        d.count = gpu[i]->indexCount;
        draws.push_back(d);
    }

    // Face highlights (hover / selection).
    for(const auto &h : scene.faceHighlights) {
        if(!h.mesh || h.face < 1 || h.face > int(h.mesh->faceRanges.size())) continue;
        GpuMesh &g = meshFor(h.mesh, u);
        if(!g.ibuf) continue;
        const auto &r = h.mesh->faceRanges[size_t(h.face) - 1];
        if(r.count == 0) continue;
        DrawCall d;
        d.pipeline = faces ? p.meshOverlay.get() : p.meshOverlayNoDepth.get();
        d.uniform = uniform(h.color, h.color, 0, kOverlayBias, 0, 0);
        d.vb0 = g.vbuf.get();
        d.ib = g.ibuf.get();
        d.count = r.count;
        d.firstIndex = r.first;
        draws.push_back(d);
    }

    // Dynamic triangles (profiles, planes...).
    std::vector<float> triData;
    for(const auto &t : scene.triangles) {
        const quint32 first = quint32(triData.size() / 6);
        for(size_t k = 0; k + 2 < t.triangles.size(); k += 3) {
            const QVector3D a = t.triangles[k], b = t.triangles[k + 1], c = t.triangles[k + 2];
            const QVector3D n = QVector3D::crossProduct(b - a, c - a).normalized();
            for(const QVector3D &v : {a, b, c}) triData.insert(triData.end(), {v.x(), v.y(), v.z(), n.x(), n.y(), n.z()});
        }
        const quint32 count = quint32(triData.size() / 6) - first;
        if(!count) continue;
        DrawCall d;
        d.pipeline = t.depthTest ? p.meshOverlay.get() : p.meshOverlayNoDepth.get();
        d.uniform = uniform(t.color, t.color, 0, kOverlayBias * 2, 0, 0);
        d.vb0Offset = first * 6 * sizeof(float);
        d.count = count;
        draws.push_back(d);
    }
    if(!triData.empty()) {
        ensureDynamicBuffer(p.dynTris, quint32(triData.size() * 4), QRhiBuffer::VertexBuffer);
        u->updateDynamicBuffer(p.dynTris.get(), 0, quint32(triData.size() * 4), triData.data());
        for(auto &d : draws)
            if((d.pipeline == p.meshOverlay.get() || d.pipeline == p.meshOverlayNoDepth.get()) && !d.vb0 && !d.ib)
                d.vb0 = p.dynTris.get();
    }

    // Dynamic lines.
    std::vector<float> lineData;
    for(const auto &l : scene.lines) {
        const quint32 first = quint32(lineData.size() / 6);
        for(size_t k = 0; k + 1 < l.segments.size(); k += 2) {
            const QVector3D a = l.segments[k], b = l.segments[k + 1];
            lineData.insert(lineData.end(), {a.x(), a.y(), a.z(), b.x(), b.y(), b.z()});
        }
        const quint32 count = quint32(lineData.size() / 6) - first;
        if(!count) continue;
        DrawCall d;
        d.pipeline = l.depthTest ? p.line.get() : p.lineNoDepth.get();
        d.uniform = uniform(l.color, l.color, l.width * dpr, l.depthTest ? kEdgeBias * 1.5f : 0.0f, 0,
                            l.ignoreClip ? 1.0f : 0.0f);
        d.vb0 = p.lineCorners.get();
        d.vb1Offset = first * 6 * sizeof(float);
        d.count = 6;
        d.instances = count;
        draws.push_back(d);
    }
    if(!lineData.empty()) {
        ensureDynamicBuffer(p.dynLines, quint32(lineData.size() * 4), QRhiBuffer::VertexBuffer);
        u->updateDynamicBuffer(p.dynLines.get(), 0, quint32(lineData.size() * 4), lineData.data());
        for(auto &d : draws)
            if((d.pipeline == p.line.get() || d.pipeline == p.lineNoDepth.get()) && !d.vb1) d.vb1 = p.dynLines.get();
    }

    // Edge highlights.
    for(const auto &h : scene.edgeHighlights) {
        if(!h.mesh || h.edge < 1 || h.edge > int(h.mesh->edgeRanges.size())) continue;
        GpuMesh &g = meshFor(h.mesh, u);
        if(!g.edgeBuf) continue;
        const auto [first, count] = g.edgeSegs[size_t(h.edge) - 1];
        if(!count) continue;
        DrawCall d;
        d.pipeline = faces ? p.line.get() : p.lineNoDepth.get();
        d.uniform = uniform(h.color, h.color, h.width * dpr, kEdgeBias * 2, 0, 0);
        d.vb0 = p.lineCorners.get();
        d.vb1 = g.edgeBuf.get();
        d.vb1Offset = first * 6 * sizeof(float);
        d.count = 6;
        d.instances = count;
        draws.push_back(d);
    }

    // Points.
    std::vector<float> pointData;
    for(const auto &pb : scene.points) {
        const quint32 first = quint32(pointData.size() / 3);
        for(const auto &pt : pb.points) pointData.insert(pointData.end(), {pt.x(), pt.y(), pt.z()});
        const quint32 count = quint32(pointData.size() / 3) - first;
        if(!count) continue;
        DrawCall d;
        d.pipeline = pb.depthTest ? p.point.get() : p.pointNoDepth.get();
        d.uniform = uniform(pb.color, pb.outline.isValid() ? pb.outline : pb.color, pb.size * dpr,
                            pb.depthTest ? kEdgeBias * 3 : 0.0f, pb.round ? 1.0f : 0.0f, 0);
        d.vb0 = p.quadCorners.get();
        d.vb1Offset = first * 3 * sizeof(float);
        d.count = 6;
        d.instances = count;
        draws.push_back(d);
    }
    if(!pointData.empty()) {
        ensureDynamicBuffer(p.dynPoints, quint32(pointData.size() * 4), QRhiBuffer::VertexBuffer);
        u->updateDynamicBuffer(p.dynPoints.get(), 0, quint32(pointData.size() * 4), pointData.data());
        for(auto &d : draws)
            if((d.pipeline == p.point.get() || d.pipeline == p.pointNoDepth.get()) && !d.vb1) d.vb1 = p.dynPoints.get();
    }

    // Upload per-draw uniforms.
    if(uniforms.size() > p.drawCapacity) {
        while(p.drawCapacity < uniforms.size()) p.drawCapacity *= 2;
        p.drawUbo.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, p.drawStride * p.drawCapacity));
        p.drawUbo->create();
        rebuildBindings();
    }
    if(!uniforms.empty()) {
        std::vector<char> blob(size_t(p.drawStride) * uniforms.size(), 0);
        for(size_t i = 0; i < uniforms.size(); ++i) std::memcpy(blob.data() + i * p.drawStride, &uniforms[i], sizeof(DrawUniforms));
        u->updateDynamicBuffer(p.drawUbo.get(), 0, quint32(blob.size()), blob.data());
    }

    // ViewCube uniforms.
    const QRect cubeRect = ViewCube::rect(cam.viewport);
    if(scene.viewCube) {
        CubeUniforms cu{};
        setMat(cu.mvp, corr * ViewCube::viewProjection(cam.rotation));
        setMat(cu.rotation, ViewCube::viewRotation(cam.rotation));
        if(scene.viewCubeHover) setVec(cu.highlight, *scene.viewCubeHover, 1.0f);
        u->updateDynamicBuffer(p.cubeUbo.get(), 0, sizeof cu, &cu);
    }

    // Record.
    const QRhiViewport full(0, 0, float(fb.width()), float(fb.height()));
    if(!silDraws.empty()) {
        cb->beginPass(p.silRt.get(), QColor(0, 0, 0, 0), {1.0f, 0}, u);
        u = nullptr;
        cb->setViewport(QRhiViewport(0, 0, 512, 512));
        for(const DrawCall &d : silDraws) {
            cb->setGraphicsPipeline(d.pipeline);
            const QRhiCommandBuffer::DynamicOffset off(1, d.uniform * p.drawStride);
            cb->setShaderResources(d.srb, 1, &off);
            const QRhiCommandBuffer::VertexInput vi(d.vb0, 0);
            cb->setVertexInput(0, 1, &vi, d.ib, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(d.count);
        }
        cb->endPass();
        // Blurred versions of it for the soft shadow.
        u = m_rhi->nextResourceUpdateBatch();
        u->generateMips(p.silTex.get());
    }
    cb->beginPass(rt, backgroundBottom, {1.0f, 0}, u);
    cb->setGraphicsPipeline(p.bg.get());
    cb->setViewport(full);
    cb->setShaderResources(p.bgSrb.get());
    cb->draw(3);

    for(const DrawCall &d : draws) {
        if(!d.vb0 || d.count == 0) continue;
        cb->setGraphicsPipeline(d.pipeline);
        if(d.pipeline == p.stencilParity.get() || d.pipeline == p.cap.get()) cb->setStencilRef(0);
        cb->setViewport(full);
        const QRhiCommandBuffer::DynamicOffset off(1, d.uniform * p.drawStride);
        cb->setShaderResources(d.srb ? d.srb : p.srb.get(), 1, &off);
        QRhiCommandBuffer::VertexInput vi[2] = {{d.vb0, d.vb0Offset}, {d.vb1, d.vb1Offset}};
        const int nvb = d.vb1 ? 2 : 1;
        if(d.ib) {
            cb->setVertexInput(0, nvb, vi, d.ib, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(d.count, d.instances, d.firstIndex);
        } else {
            cb->setVertexInput(0, nvb, vi);
            cb->draw(d.count, d.instances);
        }
    }

    if(scene.viewCube) {
        const float s = dpr;
        // QRhiViewport has a bottom-left origin; squeeze depth so the cube is always in front.
        const QRhiViewport vp(cubeRect.left() * s, float(fb.height()) - cubeRect.bottom() * s - s,
                              cubeRect.width() * s, cubeRect.height() * s, 0.0f, 0.002f);
        cb->setGraphicsPipeline(p.cube.get());
        cb->setViewport(vp);
        cb->setShaderResources(p.cubeSrb.get());
        const QRhiCommandBuffer::VertexInput vi(p.cubeVbuf.get(), 0);
        cb->setVertexInput(0, 1, &vi, p.cubeIbuf.get(), 0, QRhiCommandBuffer::IndexUInt16);
        cb->drawIndexed(p.cubeIndexCount);
    }
    cb->endPass();
    evictMeshes();
}

} // namespace cadly
