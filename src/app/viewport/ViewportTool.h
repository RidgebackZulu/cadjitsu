#pragma once

#include <Qt>

class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace cadjitsu {

struct RenderScene;

// An interactive mode that takes over left-button input in the viewport
// (sketch tools, placement tools, manipulators). Navigation with the middle
// button and the ViewCube keep working while a tool is active.
class ViewportTool {
public:
    virtual ~ViewportTool() = default;
    virtual bool mousePress(QMouseEvent *) { return false; }
    virtual bool mouseMove(QMouseEvent *) { return false; }
    virtual bool mouseRelease(QMouseEvent *) { return false; }
    virtual bool mouseDoubleClick(QMouseEvent *) { return false; }
    virtual bool keyPress(QKeyEvent *) { return false; }
    // 2D overlay in logical pixels (labels, rubber bands).
    virtual void paintOverlay(QPainter &) {}
    // Extra 3D geometry for the next frame.
    virtual void contribute(RenderScene &) {}
    virtual Qt::CursorShape cursor() const { return Qt::ArrowCursor; }
};

} // namespace cadjitsu
