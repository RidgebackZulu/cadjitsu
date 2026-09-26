#pragma once

#include <QColor>
#include <QRhiWidget>

#include <memory>

class QRhi;
class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;

namespace cadly {

// The 3D modeling viewport. Renders through Qt RHI (Metal on macOS, OpenGL on
// Linux) into the widget's backing texture.
class Viewport : public QRhiWidget {
    Q_OBJECT

public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;

    QColor backgroundTop() const { return m_bgTop; }
    QColor backgroundBottom() const { return m_bgBottom; }

    // Name of the active graphics API ("Metal", "OpenGL", ...); empty before init.
    QString backendName() const { return m_backendName; }

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

private:
    void createBackgroundPipeline();

    QRhi *m_rhi = nullptr;
    QString m_backendName;

    QColor m_bgTop{250, 251, 252};
    QColor m_bgBottom{196, 205, 216};

    std::unique_ptr<QRhiBuffer> m_bgUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_bgBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_bgPipeline;
};

} // namespace cadly
