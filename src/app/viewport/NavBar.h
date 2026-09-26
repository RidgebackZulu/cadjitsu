#pragma once

#include <QFrame>

class QToolButton;

namespace cadly {

class Viewport;

// The navigation bar at the bottom centre of the canvas (like Fusion 360):
// orbit / pan / zoom modes, fit, visual style, grid and camera projection.
class NavBar : public QFrame {
    Q_OBJECT

public:
    explicit NavBar(Viewport *viewport);
    void reposition();
    void syncFromViewport();

private:
    Viewport *m_viewport;
    QToolButton *m_orbit, *m_pan, *m_zoom, *m_fit, *m_display, *m_grid, *m_camera;
};

// Transparent child widget that paints viewport overlays with QPainter.
class ViewportOverlay : public QWidget {
public:
    explicit ViewportOverlay(Viewport *viewport);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    Viewport *m_viewport;
};

} // namespace cadly
