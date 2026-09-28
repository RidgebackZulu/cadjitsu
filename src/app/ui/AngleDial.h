#pragma once

#include <QColor>
#include <QWidget>

namespace cadjitsu {

// A small circular angle meter (a protractor dial) beside an angle value box:
// a track with ticks every 15 degrees, an accent arc from 0 to the angle, a
// knob and the value in the middle. 0 is at 3 o'clock, positive angles turn
// anticlockwise. Drag around it to set the angle (1 degree steps, 15 with
// Shift), scroll to step 1 degree, double-click for 0.
class AngleDial : public QWidget {
    Q_OBJECT

public:
    explicit AngleDial(const QColor &accent, QWidget *parent = nullptr);

    // Degrees; shown wrapped to (-180, 180]. Does not emit angleEdited().
    void setAngle(double degrees);
    double angle() const { return m_angle; }
    // Draw as unset (the value box does not evaluate).
    void setInvalid(bool on);
    // Show and set 0..360 degrees (a total angle: 360 is all the way round)
    // instead of -180..180.
    void setFullTurn(bool on) {
        m_fullTurn = on;
        update();
    }
    QColor accent() const { return m_accent; }

    QSize sizeHint() const override { return {44, 44}; }
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    // The user turned the dial (degrees).
    void angleEdited(double degrees);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void setFromPoint(QPointF p, Qt::KeyboardModifiers mods);
    void edit(double degrees);

    QColor m_accent;
    double m_angle = 0.0;
    bool m_invalid = false, m_dragging = false, m_hover = false, m_fullTurn = false;
    double shown() const;
};

// Wraps degrees to (-180, 180].
double wrapDegrees(double degrees);

} // namespace cadjitsu
