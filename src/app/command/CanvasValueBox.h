#pragma once

#include <QLineEdit>
#include <QPointer>

namespace cadly {

class ValueField;

// The value box a command shows on the canvas, next to its arrow or the last
// thing picked (Fusion's on-canvas distance / radius box). It mirrors one of
// the panel's value fields both ways: typing in either updates the other.
class CanvasValueBox : public QLineEdit {
    Q_OBJECT

public:
    explicit CanvasValueBox(QWidget *canvas);

    void bind(ValueField *field);
    ValueField *field() const { return m_field; }
    // Shows the box with its left edge at `px` (canvas pixels), or hides it.
    void showAt(QPointF px);

signals:
    void commitRequested();
    void cancelRequested();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private:
    void sync();
    void restyle();

    QPointer<ValueField> m_field;
    QMetaObject::Connection m_textConn;
};

} // namespace cadly
