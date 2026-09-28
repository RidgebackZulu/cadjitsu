#pragma once

#include "expr/Expression.h"

#include <QColor>
#include <QLineEdit>
#include <QPointer>

namespace cadjitsu {

class UnitSuffix;
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
    // Border colour (invalid: the default blue); red while the value is bad.
    void setAccent(const QColor &c);

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
    QPointer<UnitSuffix> m_unit;
    cad::ValueKind m_unitKind = cad::ValueKind::Length;
    QMetaObject::Connection m_textConn, m_unitConn;
    QColor m_accent;
};

} // namespace cadjitsu
