#pragma once

#include "sketch/SketchEditor.h"

#include <QFrame>

class QCheckBox;
class QPushButton;
class QToolButton;

namespace cadjitsu {

// Fusion 360's SKETCH PALETTE: a panel on the right of the canvas with the
// sketch display options (grid, snap, profiles, points, dimensions,
// constraints), Look At, the construction toggle and Finish Sketch.
class SketchPalette : public QFrame {
    Q_OBJECT

public:
    explicit SketchPalette(QWidget *canvas);

    void setOptions(const SketchDisplayOptions &o);
    SketchDisplayOptions options() const;
    void reposition();

signals:
    void optionsChanged(const cadjitsu::SketchDisplayOptions &o);
    void lookAtRequested();
    void constructionRequested();
    void finishRequested();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    QCheckBox *m_grid, *m_snap, *m_profile, *m_points, *m_dimensions, *m_constraints;
    QToolButton *m_lookAt, *m_construction;
    QPushButton *m_finish;
};

} // namespace cadjitsu
