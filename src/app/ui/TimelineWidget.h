#pragma once

#include "model/RecomputeService.h"

#include "doc/Document.h"

#include <QWidget>

class QToolButton;

namespace cadly {

// Fusion 360's timeline along the bottom of the window: one icon per feature
// in order, and the history marker after the last active feature. Dragging
// the marker scrubs the model through its history; right-click a feature to
// edit, suppress, rename or delete it, or roll the marker to it.
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(cad::Document &doc, QWidget *parent = nullptr);

    // Feature statuses (errors, warnings) from the latest document evaluation.
    void setEvaluation(EvaluationPtr e);
    // Re-reads the features and the marker from the document.
    void refresh();
    // While a feature is being edited the marker shows it rolled back to it.
    void setEditing(cad::FeatureId id);

    // Geometry, in widget coordinates.
    QRect itemRect(int index) const;
    int markerX() const;
    int itemAt(QPoint p) const;
    int count() const { return int(m_doc.features().size()); }
    QSize sizeHint() const override;

signals:
    void editRequested(cad::FeatureId id);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool event(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    int boundaryX(int marker) const;
    int nearestBoundary(int x) const;
    int shownMarker() const;
    void showMenu(int index, QPoint globalPos);
    cad::Status statusOf(int index) const;
    void clampScroll();

    cad::Document &m_doc;
    EvaluationPtr m_eval;
    cad::FeatureId m_editing = cad::kNoFeature;
    QToolButton *m_first, *m_back, *m_forward, *m_last;
    int m_scroll = 0;
    int m_hover = -1;
    int m_selected = -1;
    bool m_dragging = false;
    int m_dragStartMarker = 0;
    cad::json m_dragSnapshot;
};

} // namespace cadly
