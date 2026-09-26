#include "viewport/Viewport.h"

#include "viewport/ShaderLoader.h"

#include <rhi/qrhi.h>

namespace cadly {

namespace {

struct BackgroundUniforms {
    float top[4];
    float bottom[4];
    float ndcYUp;
    float pad[3];
};

void toFloat4(const QColor &c, float out[4]) {
    out[0] = float(c.redF());
    out[1] = float(c.greenF());
    out[2] = float(c.blueF());
    out[3] = 1.0f;
}

} // namespace

Viewport::Viewport(QWidget *parent) : QRhiWidget(parent) {
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

Viewport::~Viewport() = default;

void Viewport::initialize(QRhiCommandBuffer *) {
    if(m_rhi != rhi()) {
        m_bgPipeline.reset();
        m_bgBindings.reset();
        m_bgUniforms.reset();
        m_rhi = rhi();
        m_backendName = QString::fromLatin1(m_rhi->backendName());
    }
    if(!m_bgPipeline) createBackgroundPipeline();
}

void Viewport::createBackgroundPipeline() {
    m_bgUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                        sizeof(BackgroundUniforms)));
    m_bgUniforms->create();

    m_bgBindings.reset(m_rhi->newShaderResourceBindings());
    m_bgBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
        m_bgUniforms.get())});
    m_bgBindings->create();

    m_bgPipeline.reset(m_rhi->newGraphicsPipeline());
    m_bgPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral("background.vert"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral("background.frag"))},
    });
    m_bgPipeline->setVertexInputLayout({});
    m_bgPipeline->setShaderResourceBindings(m_bgBindings.get());
    m_bgPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_bgPipeline->setSampleCount(renderTarget()->sampleCount());
    m_bgPipeline->setDepthTest(false);
    m_bgPipeline->setDepthWrite(false);
    m_bgPipeline->create();
}

void Viewport::render(QRhiCommandBuffer *cb) {
    QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();

    BackgroundUniforms u{};
    toFloat4(m_bgTop, u.top);
    toFloat4(m_bgBottom, u.bottom);
    u.ndcYUp = m_rhi->isYUpInNDC() ? 1.0f : -1.0f;
    updates->updateDynamicBuffer(m_bgUniforms.get(), 0, sizeof(u), &u);

    const QSize size = renderTarget()->pixelSize();
    cb->beginPass(renderTarget(), m_bgBottom, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_bgPipeline.get());
    cb->setViewport({0, 0, float(size.width()), float(size.height())});
    cb->setShaderResources();
    cb->draw(3);
    cb->endPass();
}

void Viewport::releaseResources() {
    m_bgPipeline.reset();
    m_bgBindings.reset();
    m_bgUniforms.reset();
    m_rhi = nullptr;
}

} // namespace cadly
