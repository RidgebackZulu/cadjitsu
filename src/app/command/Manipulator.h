#pragma once

#include "viewport/ViewportTool.h"

#include <QColor>
#include <QPointF>
#include <QVector3D>

#include <functional>

namespace cadly {

class Viewport;

// An arrow on the canvas that sets a distance by dragging its head along
// its axis (Fusion's extrude arrow). Other clicks fall through to selection.
class DistanceManipulator : public ViewportTool {
public:
    explicit DistanceManipulator(Viewport *viewport);

    void setAxis(const QVector3D &origin, const QVector3D &direction);
    QVector3D origin() const { return m_origin; }
    QVector3D direction() const { return m_dir; }
    void setDistance(double d) { m_distance = d; }
    double distance() const { return m_distance; }
    void setVisible(bool on) { m_visible = on; }
    bool visible() const { return m_visible; }
    bool dragging() const { return m_dragging; }
    // The head's position on screen (logical pixels) and in the model.
    QPointF headOnScreen() const;
    QVector3D headPoint() const { return head(); }

    std::function<void(double)> onDrag;  // new distance while dragging
    std::function<void()> onRelease;
    QString label;                       // drawn next to the head

    bool mousePress(QMouseEvent *e) override;
    bool mouseMove(QMouseEvent *e) override;
    bool mouseRelease(QMouseEvent *e) override;
    void contribute(RenderScene &scene) override;
    void paintOverlay(QPainter &p) override;

private:
    QVector3D head() const { return m_origin + m_dir * float(displayLength()); }
    double displayLength() const;
    double axisParameter(QPointF px, bool *ok) const;
    bool nearHead(QPointF px) const;

    Viewport *m_viewport;
    QVector3D m_origin, m_dir{0, 0, 1};
    double m_distance = 0.0;
    bool m_visible = false;
    bool m_dragging = false, m_hot = false;
    double m_grab = 0.0;
};

} // namespace cadly
