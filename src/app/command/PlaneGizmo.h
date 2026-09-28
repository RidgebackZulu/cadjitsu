#pragma once

#include "command/Manipulator.h"

#include <QColor>
#include <QString>

#include <array>
#include <functional>

namespace cadjitsu {

// The Offset Plane command's canvas controls: the distance arrow plus up to
// two rotation rings, each a protractor about one axis through the plane's
// pivot. A ring has ticks every 15 degrees, a translucent wedge from 0 to its
// angle and a knob at the angle; drag the knob (or the ring) to turn it, in
// 1 degree steps (15 with Shift). Rings keep a constant size on screen.
class PlaneGizmo : public ViewportTool {
public:
    enum class Part { None, Arrow, Ring0, Ring1 };

    struct Ring {
        bool visible = false;
        QVector3D center;
        QVector3D axis{1, 0, 0}; // unit; turning is right-handed about it
        QVector3D zero{0, 1, 0}; // unit, across the axis: where 0 is
        double angle = 0.0;      // radians
        QColor color;
        QString name;            // "Tilt X"
    };

    explicit PlaneGizmo(Viewport *viewport);

    DistanceManipulator &arrow() { return m_arrow; }
    const DistanceManipulator &arrow() const { return m_arrow; }
    Ring &ring(int i) { return m_rings[size_t(i)]; }
    const Ring &ring(int i) const { return m_rings[size_t(i)]; }

    // A ring's radius in the model (it is a fixed number of pixels on screen).
    double radius(int i) const;
    // Where a ring's knob is, in the model and on screen (logical pixels).
    QVector3D knobPoint(int i) const;
    QPointF knobOnScreen(int i) const;
    // A point on ring `i` at `angle` (radians), on screen.
    QPointF ringPointOnScreen(int i, double angle) const;

    Part hot() const { return m_hot; }
    Part dragged() const { return m_drag; }
    // The part last hovered or dragged: the one typed values go to.
    Part focus() const { return m_focus; }
    void setFocus(Part p);

    std::function<void(int ring, double angle)> onRingDrag; // new angle (radians) while dragging
    std::function<void()> onFocusChanged;
    std::function<void()> onRelease;

    bool mousePress(QMouseEvent *e) override;
    bool mouseMove(QMouseEvent *e) override;
    bool mouseRelease(QMouseEvent *e) override;
    void contribute(RenderScene &scene) override;
    void paintOverlay(QPainter &p) override;
    Qt::CursorShape cursor() const override;

private:
    Part hitTest(QPointF px) const;
    // The angle (radians) of the point under `px` on ring i's plane.
    bool angleAt(int i, QPointF px, double *angle) const;
    void setHot(Part p);

    Viewport *m_viewport;
    DistanceManipulator m_arrow;
    std::array<Ring, 2> m_rings;
    Part m_hot = Part::None, m_drag = Part::None, m_focus = Part::Arrow;
    double m_grab = 0.0;
};

} // namespace cadjitsu
