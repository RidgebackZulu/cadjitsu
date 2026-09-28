#include "command/CanvasValueBox.h"

#include "command/CommandPanel.h"
#include "ui/Units.h"

#include <QKeyEvent>
#include <QTimer>

namespace cadly {

CanvasValueBox::CanvasValueBox(QWidget *canvas) : QLineEdit(canvas) {
    setObjectName(QStringLiteral("canvasValue"));
    setAlignment(Qt::AlignCenter);
    hide();
    connect(this, &QLineEdit::textEdited, this, [this](const QString &t) {
        if(m_field) m_field->enterInput(t, m_unit ? m_unit->unit() : QString());
        restyle();
    });
    restyle();
}

void CanvasValueBox::bind(ValueField *field) {
    if(field == m_field) return;
    disconnect(m_textConn);
    disconnect(m_unitConn);
    m_field = field;
    // A unit drop-down like the field's (lengths and angles).
    if(m_unit && (!field || field->kind() != m_unitKind)) {
        delete m_unit;
        m_unit = nullptr;
        setTextMargins(0, 0, 0, 0);
    }
    if(field && !m_unit) {
        m_unitKind = field->kind();
        m_unit = UnitSuffix::attach(this, field->kind());
        if(m_unit)
            connect(m_unit, &UnitSuffix::unitPicked, this, [this](const QString &u) {
                if(m_field) m_field->setUnit(u);
            });
    }
    if(field) {
        m_textConn = connect(field, &QLineEdit::textChanged, this, &CanvasValueBox::sync);
        m_unitConn = connect(field, &ValueField::unitChanged, this, &CanvasValueBox::sync);
    }
    sync();
}

void CanvasValueBox::sync() {
    if(!m_field) return;
    if(m_unit && m_unit->unit() != m_field->unit()) m_unit->setUnit(m_field->unit());
    if(text() != m_field->text()) {
        const bool typing = hasFocus();
        setText(m_field->text());
        if(!typing) setCursorPosition(0);
    }
    const int suffix = m_unit ? m_unit->sizeHint().width() + 2 : 0;
    const int w = std::clamp(fontMetrics().horizontalAdvance(text()) + 22 + suffix, 64 + suffix, 220);
    resize(w, 22);
    restyle();
}

void CanvasValueBox::restyle() {
    const bool bad = m_field && !m_field->valid() && !m_field->expression().isEmpty(); // empty: waiting, not wrong
    setStyleSheet(QStringLiteral("QLineEdit#%2 { background: rgba(255, 255, 255, 245); border: 1px solid %1;"
                                 " border-radius: 3px; padding: 1px 4px; color: #10223c; font-size: 12px;"
                                 " selection-background-color: #9cc6ff; selection-color: #10223c; }"
                                 " #unitSuffix { border: none; }")
                      .arg(bad ? QStringLiteral("#d23c3c")
                               : m_accent.isValid() ? m_accent.name() : QStringLiteral("#1a65c9"),
                           objectName()));
}

void CanvasValueBox::setAccent(const QColor &c) {
    if(c == m_accent) return;
    m_accent = c;
    restyle();
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
