#include "command/CommandPanel.h"

#include "ui/Icons.h"
#include "viewport/ViewCube.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace cadly {

// --- ValueField ------------------------------------------------------------------

ValueField::ValueField(cad::ValueKind kind, Evaluator eval, QWidget *parent)
    : QLineEdit(parent), m_kind(kind), m_eval(std::move(eval)) {
    setMinimumWidth(110);
    connect(this, &QLineEdit::textEdited, this, [this] {
        revalidate();
        emit edited();
    });
    revalidate();
}

void ValueField::setExpression(const QString &expr) {
    setText(expr);
    revalidate();
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
    setStyleSheet(m_value ? QString() : QStringLiteral("QLineEdit { border: 1px solid #d23c3c; background: #fff3f2; }"));
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

void SelectionField::setActive(bool on) {
    m_active = on;
    restyle();
}

void SelectionField::mousePressEvent(QMouseEvent *e) {
    if(e->button() == Qt::LeftButton) emit activated();
}

void SelectionField::restyle() {
    m_text->setText(m_count > 0 ? tr("%n selected", nullptr, m_count) : m_hint);
    m_clear->setVisible(m_count > 0);
    setStyleSheet(m_active ? QStringLiteral("cadly--SelectionField { border: 1px solid #1a66c9; border-radius: 3px;"
                                            " background: #e3efff; }")
                           : QStringLiteral("cadly--SelectionField { border: 1px solid #b9c1cc; border-radius: 3px;"
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
        "#commandCancel { border-radius: 3px; padding: 4px 12px; }"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(10, 8, 10, 10);
    v->setSpacing(6);
    auto *titleRow = new QHBoxLayout;
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
    m_nextRow = 0;
    m_title->setText(title.toUpper());
    Q_UNUSED(id);
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

QCheckBox *CommandPanel::addCheck(const QString &label, const char *name) {
    auto *c = new QCheckBox(m_body);
    c->setObjectName(QString::fromLatin1(name));
    c->setFocusPolicy(Qt::NoFocus);
    addRow(label, c);
    return c;
}

QLabel *CommandPanel::addSection(const QString &title) {
    auto *l = new QLabel(title.toUpper(), m_body);
    l->setObjectName(QStringLiteral("commandSection"));
    m_rows->addWidget(l, m_nextRow++, 0, 1, 2);
    return l;
}

void CommandPanel::setRowVisible(QWidget *field, bool visible) {
    field->setVisible(visible);
    auto it = m_labels.find(field);
    if(it != m_labels.end()) it->second->setVisible(visible);
    adjustSize();
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

} // namespace cadly
