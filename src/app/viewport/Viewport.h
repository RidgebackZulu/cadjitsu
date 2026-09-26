#pragma once

#include "viewport/Camera.h"
#include "viewport/Picker.h"
#include "viewport/RenderScene.h"
#include "viewport/Renderer.h"

#include <QColor>
#include <QPointer>
#include <QRhiWidget>

#include <functional>
#include <memory>
#include <optional>

class QToolButton;
class QVariantAnimation;

namespace cadly {

class NavBar;
class ViewportOverlay;
class ViewportTool;

// The 3D modeling viewport with Fusion 360 navigation:
//   middle drag = pan, Shift + middle drag = orbit, wheel = zoom at cursor,
//   double-click middle = fit, trackpad: two-finger drag = pan (Shift = orbit),
//   pinch = zoom. Left click selects, left drag makes a window (left-to-right)
//   or crossing (right-to-left) selection. ViewCube in the top-right corner.
class Viewport : public QRhiWidget {
    Q_OBJECT

public:
    enum class NavMode { Select, Orbit, Pan, Zoom };

    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;

    Camera &camera() { return m_camera; }
    const Camera &camera() const { return m_camera; }

    // Content supplied by the application.
    void setContent(const RenderScene &scene, std::vector<PickTarget> targets);
    const RenderScene &content() const { return m_content; }
    const std::vector<PickTarget> &pickTargets() const { return m_targets; }
    void setHighlights(std::vector<FaceHighlight> faces, std::vector<EdgeHighlight> edges,
                       std::vector<PointBatch> points = {});

    DisplayStyle displayStyle() const { return m_style; }
    void setDisplayStyle(DisplayStyle s);
    bool gridVisible() const { return m_grid; }
    void setGridVisible(bool on);
    void setOrthographic(bool on);
    void setClipPlane(std::optional<QVector4D> plane);
    std::optional<QVector4D> clipPlane() const { return m_clip; }
    PickOptions &pickOptions() { return m_pickOptions; }

    void setTool(ViewportTool *tool);
    ViewportTool *tool() const { return m_tool; }
    NavMode navMode() const { return m_navMode; }
    void setNavMode(NavMode m);

    // Views.
    void fitAll(bool animate = true);
    void setStandardView(StandardView v, bool animate = true);
    void animateTo(const QQuaternion &rotation, const QVector3D &target, float distance);
    Box3 contentBounds() const;

    // Picking helpers (logical pixel coordinates).
    PickHit pickAt(QPointF px) const;
    std::optional<QVector3D> raycast(QPointF px) const;
    PickHit hovered() const { return m_hover; }

    QString backendName() const { return m_backendName; }
    QColor backgroundTop() const { return m_renderer.backgroundTop; }
    QColor backgroundBottom() const { return m_renderer.backgroundBottom; }
    int frameCount() const { return m_frames; }

    // Paints overlays (selection rectangle, tool labels) - called by the overlay widget.
    void paintOverlay(QPainter &p);
    // Schedules a repaint of the 3D view and its overlay (after tool state changes).
    void refreshOverlay();

signals:
    void hoverChanged(const cadly::PickHit &hit);
    void clicked(const cadly::PickHit &hit, Qt::KeyboardModifiers modifiers);
    void doubleClicked(const cadly::PickHit &hit);
    void boxSelected(const QRectF &rect, bool crossing, Qt::KeyboardModifiers modifiers);
    void contextMenuRequested(const QPoint &globalPos, const cadly::PickHit &hit);
    void escapePressed();
    void cameraChanged();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

    bool event(QEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    enum class Drag { None, Pan, Orbit, Zoom, Box, Cube };

    void updateHover(QPointF pos);
    void updateGrid();
    void stopAnimation();
    QVector3D anchorAt(QPointF px) const;
    void changed();

    Renderer m_renderer;
    Camera m_camera;
    RenderScene m_content;
    std::vector<PickTarget> m_targets;
    std::vector<FaceHighlight> m_faceHi;
    std::vector<EdgeHighlight> m_edgeHi;
    std::vector<PointBatch> m_pointHi;
    DisplayStyle m_style = DisplayStyle::ShadedWithEdges;
    bool m_grid = true;
    std::optional<QVector4D> m_clip;
    PickOptions m_pickOptions;

    ViewportTool *m_tool = nullptr;
    NavMode m_navMode = NavMode::Select;
    Drag m_drag = Drag::None;
    QPointF m_pressPos, m_lastPos;
    Qt::MouseButton m_dragButton = Qt::NoButton;
    QVector3D m_pivot;
    bool m_dragMoved = false;
    PickHit m_hover;
    std::optional<QVector3D> m_cubeHover;
    QRectF m_box;

    QString m_backendName;
    int m_frames = 0;
    bool m_firstFit = true;

    ViewportOverlay *m_overlay = nullptr;
    NavBar *m_navBar = nullptr;
    QToolButton *m_homeButton = nullptr;
    QVariantAnimation *m_anim = nullptr;
};

} // namespace cadly
