#include "command/CanvasValueBox.h"

#include "command/CommandPanel.h"

#include <QKeyEvent>
#include <QTimer>

namespace cadly {

CanvasValueBox::CanvasValueBox(QWidget *canvas) : QLineEdit(canvas) {
    setObjectName(QStringLiteral("canvasValue"));
    setAlignment(Qt::AlignCenter);
    hide();
    connect(this, &QLineEdit::textEdited, this, [this](const QString &t) {
        if(m_field) m_field->enterExpression(t);
        restyle();
    });
    restyle();
}

void CanvasValueBox::bind(ValueField *field) {
    if(field == m_field) return;
    disconnect(m_textConn);
    m_field = field;
    if(field) m_textConn = connect(field, &QLineEdit::textChanged, this, &CanvasValueBox::sync);
    sync();
}

void CanvasValueBox::sync() {
    if(!m_field) return;
    if(text() != m_field->text()) {
        const bool typing = hasFocus();
        setText(m_field->text());
        if(!typing) setCursorPosition(0);
    }
    const int w = std::clamp(fontMetrics().horizontalAdvance(text()) + 22, 64, 220);
    resize(w, 22);
    restyle();
}

void CanvasValueBox::restyle() {
    const bool bad = m_field && !m_field->valid();
    setStyleSheet(QStringLiteral("#canvasValue { background: rgba(255, 255, 255, 245); border: 1px solid %1;"
                                 " border-radius: 3px; padding: 1px 4px; color: #10223c; font-size: 12px;"
                                 " selection-background-color: #9cc6ff; selection-color: #10223c; }")
                      .arg(bad ? QStringLiteral("#d23c3c") : QStringLiteral("#1a65c9")));
}

void CanvasValueBox::showAt(QPointF px) {
    move(int(px.x()), int(px.y() - height() / 2.0));
    if(!isVisible()) {
        show();
        raise();
    }
}

void CanvasValueBox::keyPressEvent(QKeyEvent *e) {
    if(e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        emit commitRequested();
        return;
    }
    if(e->key() == Qt::Key_Escape) {
        emit cancelRequested();
        return;
    }
    QLineEdit::keyPressEvent(e);
}

void CanvasValueBox::focusInEvent(QFocusEvent *e) {
    QLineEdit::focusInEvent(e);
    QTimer::singleShot(0, this, [this] {
        if(hasFocus()) selectAll();
    });
}

} // namespace cadly
