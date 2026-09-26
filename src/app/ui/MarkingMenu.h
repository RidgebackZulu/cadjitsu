#pragma once

#include <QPointer>
#include <QWidget>

#include <vector>

class QAction;

namespace cadly {

// Fusion 360's right-click marking menu: up to eight commands on a ring
// around the cursor (click one, or move towards it and click) and a list of
// more commands below the ring. It is an overlay on the canvas; a click
// anywhere else or Esc closes it.
class MarkingMenu : public QWidget {
    Q_OBJECT

public:
    explicit MarkingMenu(QWidget *canvas);

    // Slots clockwise from the top: N, NE, E, SE, S, SW, W, NW (nullptr = empty).
    void setRing(const std::vector<QAction *> &actions);
    void setList(const std::vector<QAction *> &actions);
    void open(QPoint canvasPos);
    void close();
    bool isOpen() const { return isVisible(); }

    // Geometry (tests).
    QPoint slotCenter(int slot) const;
    QRect listRow(int row) const;
    int slotAt(QPoint p) const;
    int rowAt(QPoint p) const;

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QRect slotRect(int slot) const;
    void trigger(QAction *a);

    std::vector<QPointer<QAction>> m_ring, m_list;
    QPoint m_center;
    int m_hotSlot = -1, m_hotRow = -1;
};

} // namespace cadly
