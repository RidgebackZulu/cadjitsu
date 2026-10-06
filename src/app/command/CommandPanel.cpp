#include "command/CommandPanel.h"

#include "sketch/SketchEditor.h"
#include "ui/AngleDial.h"
#include "ui/Icons.h"
#include "ui/Units.h"
#include "viewport/ViewCube.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

namespace cadjitsu {

// --- ValueField ------------------------------------------------------------------

ValueField::ValueField(cad::ValueKind kind, Evaluator eval, QWidget *parent)
    : QLineEdit(parent), m_kind(kind), m_eval(std::move(eval)) {
    setMinimumWidth(110);
    m_unit = UnitSuffix::attach(this, kind);
    if(m_unit)
        connect(m_unit, &UnitSuffix::unitPicked, this, [this](const QString &u) {
            revalidate();
            emit unitChanged(u);
            emit edited();
        });
    connect(this, &QLineEdit::textEdited, this, [this] {
        m_selectOnClick = false;
        revalidate();
        emit edited();
    });
    revalidate();
}

QString ValueField::expression() const { return units::toExpression(text(), unit(), m_kind); }

QString ValueField::unit() const { return m_unit ? m_unit->unit() : QString(); }

void ValueField::setUnit(const QString &u) {
    if(!m_unit || u == m_unit->unit()) return;
    m_unit->setUnit(u);
    revalidate();
    emit unitChanged(u);
    emit edited();
}

void ValueField::setExpression(const QString &expr) {
    if(m_unit) {
        m_unit->resetToDefault();
        emit unitChanged(m_unit->unit());
    }
    setText(units::displayText(expr, m_kind));
    m_selectOnClick = true;
    revalidate();
}

void ValueField::enterExpression(const QString &expr) {
    const QString shown = units::displayText(expr, m_kind);
    const bool sameUnit = !m_unit || m_unit->unit() == units::defaultUnit(m_kind);
    if(text() == shown && sameUnit) return;
    if(m_unit && !sameUnit) {
        m_unit->resetToDefault();
        emit unitChanged(m_unit->unit());
    }
    setText(shown);
    revalidate();
    emit edited();
}

void ValueField::enterInput(const QString &t, const QString &u) {
    if(text() == t && unit() == u) return;
    if(m_unit && !u.isEmpty() && u != m_unit->unit()) {
        m_unit->setUnit(u);
        emit unitChanged(u);
    }
    setText(t);
    m_selectOnClick = false;
    revalidate();
    emit edited();
}

void ValueField::mouseReleaseEvent(QMouseEvent *e) {
    QLineEdit::mouseReleaseEvent(e);
    if(e->button() == Qt::LeftButton && m_selectOnClick && !hasSelectedText()) selectAll();
    m_selectOnClick = false;
}

void ValueField::focusInEvent(QFocusEvent *e) {
    QLineEdit::focusInEvent(e);
    m_selectOnClick = true;
    // After the click that focused it has placed the cursor.
    QTimer::singleShot(0, this, [this] {
        if(hasFocus()) selectAll();
    });
}

void ValueField::revalidate() {
    m_value.reset();
    const std::string t = expression().toStdString();
    QString tip;
    if(!t.empty() && m_eval) {
        const cad::EvalResult r = m_eval(t, m_kind);
        if(r.ok) m_value = r.value;
        else tip = QString::fromStdString(r.error);
    }
    setToolTip(tip);
    if(m_unit) m_unit->setActive(units::isNumber(text()) || text().trimmed().isEmpty());
    // Styled by the panel (red when the value does not evaluate; not while
    // still empty, waiting for a value).
    const bool invalid = !m_value && !text().trimmed().isEmpty();
    if(property("invalid").toBool() != invalid) {
        setProperty("invalid", invalid);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }
    emit revalidated();
}

// --- SelectionField ----------------------------------------------------------------

SelectionField::SelectionField(const QString &hint, QWidget *parent) : QFrame(parent), m_hint(hint) {
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(6, 2, 2, 2);
    h->setSpacing(2);
    m_text = new QLabel(this);
    m_clear = new QToolButton(this);
    m_clear->setText(QStringLiteral("×"));
    m_clear->setAutoRaise(true);
    m_clear->setFocusPolicy(Qt::NoFocus);
    m_clear->setToolTip(tr("Clear the selection"));
    h->addWidget(m_text, 1);
    h->addWidget(m_clear);
    setCursor(Qt::PointingHandCursor);
    connect(m_clear, &QToolButton::clicked, this, &SelectionField::cleared);
    restyle();
}

void SelectionField::setCount(int n) {
    m_count = n;
    restyle();
}

void SelectionField::setDetail(const QString &detail) {
    m_detail = detail;
    restyle();
}

QString SelectionField::text() const { return m_text->text(); }

void SelectionField::setActive(bool on) {
    m_active = on;
    restyle();
}

void SelectionField::mousePressEvent(QMouseEvent *e) {
    if(e->button() == Qt::LeftButton) emit activated();
}

void SelectionField::restyle() {
    m_text->setText(m_count <= 0 ? m_hint : m_detail.isEmpty() ? tr("%n selected", nullptr, m_count) : m_detail);
    m_clear->setVisible(m_count > 0);
    setStyleSheet(m_active ? QStringLiteral("cadjitsu--SelectionField { border: 1px solid #1a66c9; border-radius: 3px;"
                                            " background: #e3efff; }")
                           : QStringLiteral("cadjitsu--SelectionField { border: 1px solid #b9c1cc; border-radius: 3px;"
                                            " background: white; }"));
}

// --- CommandPanel -------------------------------------------------------------------

CommandPanel::CommandPanel(QWidget *canvas) : QFrame(canvas) {
    setObjectName(QStringLiteral("commandPanel"));
    setStyleSheet(QStringLiteral(
        "#commandPanel { background: rgba(250, 251, 253, 245); border: 1px solid rgba(120, 130, 145, 140);"
        " border-radius: 6px; }"
        "#commandTitle { font-weight: 700; font-size: 11px; color: #2c333d; letter-spacing: 1px; }"
        "#commandSection { font-weight: 600; font-size: 10px; color: #5a6270; }"
        "QLabel { font-size: 11px; color: #2c333d; }"
        "#commandMessage { font-size: 11px; }"
        "#commandOk { background: #1a66c9; color: white; border-radius: 3px; padding: 4px 16px; border: none; }"
        "#commandOk:disabled { background: #9db6d8; }"
        "#commandCancel { border-radius: 3px; padding: 4px 12px; }"
        "cadjitsu--ValueField { border: 1px solid #b9c0ca; border-radius: 3px; padding: 2px 4px; background: white;"
        " color: #10161f; selection-background-color: #9cc6ff; selection-color: #10161f; font-size: 12px; }"
        "QComboBox, QCheckBox, QPushButton { color: #1c2128; font-size: 11px; }"
        "QPlainTextEdit { border: 1px solid #b9c0ca; border-radius: 3px; background: white; color: #10161f;"
        " selection-background-color: #9cc6ff; selection-color: #10161f; font-size: 12px; }"
        "QPlainTextEdit:focus { border: 1px solid #1a66c9; }"
        "cadjitsu--ValueField:focus { border: 1px solid #1a66c9; }"
        "cadjitsu--ValueField[invalid=\"true\"] { border: 1px solid #d23c3c; background: #fff3f2; }"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 10);
    v->setSpacing(6);
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(7);
    m_titleIcon = new QLabel(this);
    m_titleIcon->setObjectName(QStringLiteral("commandIcon"));
    m_titleIcon->setFixedSize(22, 22);
    titleRow->addWidget(m_titleIcon);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("commandTitle"));
    titleRow->addWidget(m_title, 1);
    v->addLayout(titleRow);
    m_body = new QWidget(this);
    m_rows = new QGridLayout(m_body);
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setHorizontalSpacing(10);
    m_rows->setVerticalSpacing(5);
    m_rows->setColumnStretch(1, 1);
    v->addWidget(m_body);
    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("commandMessage"));
    m_message->setWordWrap(true);
    m_message->hide();
    v->addWidget(m_message);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_ok = new QPushButton(tr("OK"), this);
    m_ok->setObjectName(QStringLiteral("commandOk"));
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->setObjectName(QStringLiteral("commandCancel"));
    m_ok->setFocusPolicy(Qt::NoFocus);
    m_cancel->setFocusPolicy(Qt::NoFocus);
    buttons->addWidget(m_ok);
    buttons->addWidget(m_cancel);
    v->addLayout(buttons);
    connect(m_ok, &QPushButton::clicked, this, &CommandPanel::accepted);
    connect(m_cancel, &QPushButton::clicked, this, &CommandPanel::cancelled);
    setFixedWidth(300);
    canvas->installEventFilter(this);
    hide();
}

void CommandPanel::begin(const QString &title, IconId id) {
    // Drop the previous command's rows.
    // They go at once (a handler of one of them may be running, so they are
    // deleted from the event loop).
    while(QLayoutItem *item = m_rows->takeAt(0)) {
        if(QWidget *w = item->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        delete item;
    }
    m_labels.clear();
    m_rowWidgets.clear();
    m_dials.clear();
    m_nextRow = 0;
    m_title->setText(title.toUpper());
    m_titleIcon->setPixmap(icon(id).pixmap(QSize(22, 22), devicePixelRatioF()));
    setMessage({});
    setOkEnabled(true);
    show();
    reposition();
    raise();
}

void CommandPanel::end() { hide(); }

void CommandPanel::addRow(const QString &label, QWidget *field) {
    auto *l = new QLabel(label, m_body);
    m_rows->addWidget(l, m_nextRow, 0);
    m_rows->addWidget(field, m_nextRow, 1);
    // Shown now, not from the event loop: commands put the keyboard into
    // their first value box right away.
    l->show();
    field->show();
    m_labels[field] = l;
    ++m_nextRow;
    adjustSize();
}

SelectionField *CommandPanel::addSelection(const QString &label, const QString &hint, const char *name) {
    auto *f = new SelectionField(hint, m_body);
    f->setObjectName(QString::fromLatin1(name));
    addRow(label, f);
    return f;
}

QComboBox *CommandPanel::addChoice(const QString &label, const QStringList &options, const char *name) {
    auto *c = new QComboBox(m_body);
    c->setObjectName(QString::fromLatin1(name));
    c->addItems(options);
    c->setFocusPolicy(Qt::NoFocus);
    addRow(label, c);
    return c;
}

ValueField *CommandPanel::addValue(const QString &label, cad::ValueKind kind, ValueField::Evaluator eval,
                                   const char *name) {
    auto *f = new ValueField(kind, std::move(eval), m_body);
    f->setObjectName(QString::fromLatin1(name));
    f->installEventFilter(this);
    addRow(label, f);
    return f;
}

ValueField *CommandPanel::addAngle(const QString &label, ValueField::Evaluator eval, const char *name,
                                   const QColor &accent) {
    auto *row = new QWidget(m_body);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto *dial = new AngleDial(accent, row);
    dial->setObjectName(QString::fromLatin1(name) + QStringLiteral("Dial"));
    auto *f = new ValueField(cad::ValueKind::Angle, std::move(eval), row);
    f->setObjectName(QString::fromLatin1(name));
    f->installEventFilter(this);
    h->addWidget(dial);
    h->addWidget(f, 1);
    addRow(label, row);
    m_labels[f] = m_labels[row];
    m_rowWidgets[f] = row;
    m_dials[f] = dial;
    // The dial shows the box's value; turning it types into the box.
    connect(f, &ValueField::revalidated, dial, [f, dial] {
        if(f->valid()) dial->setAngle(*f->value() * 180.0 / cad::kPi);
        dial->setInvalid(!f->valid());
    });
    connect(dial, &AngleDial::angleEdited, f, [f](double deg) {
        f->enterExpression(QString::fromStdString(SketchEditor::formatExpression(deg * cad::kPi / 180.0,
                                                                                 cad::ValueKind::Angle)));
    });
    return f;
}

AngleDial *CommandPanel::angleDial(ValueField *field) const {
    auto it = m_dials.find(field);
    return it == m_dials.end() ? nullptr : it->second;
}

QCheckBox *CommandPanel::addCheck(const QString &label, const char *name) {
    auto *c = new QCheckBox(m_body);
    c->setObjectName(QString::fromLatin1(name));
    c->setFocusPolicy(Qt::NoFocus);
    addRow(label, c);
    return c;
}

QPlainTextEdit *CommandPanel::addTextBox(const QString &label, const char *name) {
    auto *t = new QPlainTextEdit(m_body);
    t->setObjectName(QString::fromLatin1(name));
    t->setTabChangesFocus(true);
    t->setLineWrapMode(QPlainTextEdit::NoWrap);
    t->setPlaceholderText(tr("Type the text"));
    t->setFixedHeight(3 * t->fontMetrics().lineSpacing() + 14);
    t->installEventFilter(this);
    addRow(label, t);
    return t;
}

QLabel *CommandPanel::addInfo(const QString &label, const char *name) {
    auto *l = new QLabel(m_body);
    l->setObjectName(QString::fromLatin1(name));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    l->setMinimumWidth(150);
    l->setStyleSheet(QStringLiteral("color: #16202c; font-weight: 600;"));
    addRow(label, l);
    return l;
}

QPushButton *CommandPanel::addButton(const QString &label, const QString &text, const char *name) {
    auto *b = new QPushButton(text, m_body);
    b->setObjectName(QString::fromLatin1(name));
    b->setFocusPolicy(Qt::NoFocus);
    addRow(label, b);
    return b;
}

QLabel *CommandPanel::addSection(const QString &title) {
    auto *l = new QLabel(title.toUpper(), m_body);
    l->setObjectName(QStringLiteral("commandSection"));
    m_rows->addWidget(l, m_nextRow++, 0, 1, 2);
    l->show();
    return l;
}

void CommandPanel::setRowVisible(QWidget *field, bool visible) {
    if(auto row = m_rowWidgets.find(field); row != m_rowWidgets.end()) field = row->second;
    field->setVisible(visible);
    auto it = m_labels.find(field);
    if(it != m_labels.end()) it->second->setVisible(visible);
    adjustSize();
}

void CommandPanel::setRowLabel(QWidget *field, const QString &label) {
    if(auto it = m_labels.find(field); it != m_labels.end()) it->second->setText(label);
}

void CommandPanel::setMessage(const QString &text, cad::Severity severity) {
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
    const char *color = severity == cad::Severity::Error ? "#b3261e" : severity == cad::Severity::Warning ? "#8a6100" : "#3c4450";
    m_message->setStyleSheet(QStringLiteral("color: %1;").arg(QString::fromLatin1(color)));
    adjustSize();
}

QString CommandPanel::message() const { return m_message->isVisible() ? m_message->text() : QString(); }

void CommandPanel::setOkEnabled(bool on) { m_ok->setEnabled(on); }

void CommandPanel::reposition() {
    QWidget *canvas = parentWidget();
    if(!canvas) return;
    adjustSize();
    const QRect cube = ViewCube::rect(canvas->size());
    move(canvas->width() - width() - 12, cube.bottom() + 16);
}

bool CommandPanel::eventFilter(QObject *o, QEvent *e) {
    if(o == parentWidget() && e->type() == QEvent::Resize) reposition();
    if(e->type() == QEvent::KeyPress && qobject_cast<QPlainTextEdit *>(o)) {
        // Handled once the text box's key event is over: accepting hides the
        // panel, and with it the text box (macOS's text input does not take
        // its focus widget going away inside its own key press).
        auto *k = static_cast<QKeyEvent *>(e);
        if((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && !(k->modifiers() & Qt::ShiftModifier)) {
            QTimer::singleShot(0, this, [this] {
                if(isVisible()) emit accepted();
            });
            return true;
        }
        if(k->key() == Qt::Key_Escape) {
            QTimer::singleShot(0, this, [this] {
                if(isVisible()) emit cancelled();
            });
            return true;
        }
    }
    if(e->type() == QEvent::KeyPress && qobject_cast<ValueField *>(o)) {
        auto *k = static_cast<QKeyEvent *>(e);
        if(k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            emit accepted();
            return true;
        }
        if(k->key() == Qt::Key_Escape) {
            emit cancelled();
            return true;
        }
    }
    return QFrame::eventFilter(o, e);
}

void CommandPanel::keyPressEvent(QKeyEvent *e) {
    if(e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) emit accepted();
    else if(e->key() == Qt::Key_Escape) emit cancelled();
    else QFrame::keyPressEvent(e);
}

} // namespace cadjitsu
