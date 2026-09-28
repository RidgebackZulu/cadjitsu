#include "sketch/HeadsUpInput.h"

#include "base/Vec2.h"
#include "ui/Units.h"

#include <QKeyEvent>
#include <QTimer>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace cadjitsu {

namespace {

const char *kBoxStyle = "QLineEdit { background: rgba(255, 255, 255, 240); border: 1px solid %1; border-radius: 3px;"
                        " padding: 1px 4px; color: %2; font-size: 12px; selection-background-color: #9cc6ff; }";

// The window's focus widget, so this also holds while the window is inactive.
bool isFocused(const QWidget *w) { return w && w->window()->focusWidget() == w; }

} // namespace

HeadsUpInput::HeadsUpInput(QWidget *host, Evaluator eval, QObject *parent)
    : QObject(parent), m_host(host), m_eval(std::move(eval)) {}

HeadsUpInput::~HeadsUpInput() {
    for(auto &e : m_entries) delete e.edit;
}

QString HeadsUpInput::formatLive(double value, cad::ValueKind kind) {
    if(kind == cad::ValueKind::Angle) return units::formatInDefault(value, kind, 1);
    if(kind == cad::ValueKind::Scalar) return QString::number(value, 'g', 6);
    const QString u = units::defaultUnit(kind);
    return units::formatInDefault(value, kind, u == QLatin1String("in") || u == QLatin1String("ft") ? 3 : 2);
}

void HeadsUpInput::setFields(const std::vector<Field> &fields) {
    bool same = fields.size() == m_entries.size();
    for(size_t i = 0; same && i < fields.size(); ++i)
        same = fields[i].name == m_entries[i].def.name && fields[i].kind == m_entries[i].def.kind;
    if(same) {
        unlockAll();
        hide();
        return;
    }
    for(auto &e : m_entries) delete e.edit;
    m_entries.clear();
    for(size_t i = 0; i < fields.size(); ++i) {
        Entry e;
        e.def = fields[i];
        e.edit = new QLineEdit(m_host);
        e.edit->setObjectName(QStringLiteral("hud_") + fields[i].name.toLower());
        e.edit->setToolTip(fields[i].name);
        e.edit->setFixedWidth(104);
        e.edit->setAlignment(Qt::AlignCenter);
        e.edit->hide();
        e.edit->installEventFilter(this);
        const int idx = int(i);
        connect(e.edit, &QLineEdit::textEdited, this, [this, idx] { onEdited(idx); });
        e.unit = UnitSuffix::attach(e.edit, fields[i].kind);
        if(e.unit)
            connect(e.unit, &UnitSuffix::unitPicked, this, [this, idx] {
                if(!m_entries[size_t(idx)].edit->text().trimmed().isEmpty()) onEdited(idx);
            });
        m_entries.push_back(e);
        updateStyle(int(i));
    }
    m_active = 0;
}

void HeadsUpInput::hide() {
    const bool hadFocus = hasFocus();
    for(auto &e : m_entries) e.edit->hide();
    if(hadFocus) m_host->setFocus(Qt::OtherFocusReason);
}

bool HeadsUpInput::visible() const {
    return std::any_of(m_entries.begin(), m_entries.end(), [](const Entry &e) { return e.edit->isVisible(); });
}

void HeadsUpInput::setLive(int i, double value) {
    if(i < 0 || i >= count()) return;
    Entry &e = m_entries[size_t(i)];
    if(e.locked || isFocused(e.edit)) return;
    if(e.unit) e.unit->resetToDefault();
    e.edit->setText(formatLive(value, e.def.kind));
}

void HeadsUpInput::place(int i, QPointF px) {
    if(i < 0 || i >= count()) return;
    QLineEdit *ed = m_entries[size_t(i)].edit;
    const QSize s = ed->sizeHint().expandedTo(QSize(ed->width(), 0));
    QPoint tl(int(px.x() - s.width() / 2.0), int(px.y() - s.height() / 2.0));
    tl.setX(std::clamp(tl.x(), 2, std::max(2, m_host->width() - s.width() - 2)));
    tl.setY(std::clamp(tl.y(), 2, std::max(2, m_host->height() - s.height() - 2)));
    ed->move(tl);
    if(!ed->isVisible()) {
        ed->show();
        ed->raise();
    }
}

bool HeadsUpInput::locked(int i) const { return i >= 0 && i < count() && m_entries[size_t(i)].locked; }

std::optional<double> HeadsUpInput::value(int i) const {
    if(!locked(i)) return std::nullopt;
    return m_entries[size_t(i)].value;
}

QString HeadsUpInput::expression(int i) const {
    if(i < 0 || i >= count()) return {};
    const Entry &e = m_entries[size_t(i)];
    return units::toExpression(e.edit->text(), e.unit ? e.unit->unit() : QString(), e.def.kind);
}

void HeadsUpInput::unlockAll() {
    for(int i = 0; i < count(); ++i) {
        m_entries[size_t(i)].locked = false;
        m_entries[size_t(i)].value.reset();
        updateStyle(i);
    }
    m_active = 0;
    if(hasFocus()) m_host->setFocus(Qt::OtherFocusReason);
}

void HeadsUpInput::beginTyping(const QString &text) {
    if(m_entries.empty()) return;
    m_active = std::clamp(m_active, 0, count() - 1);
    Entry &e = m_entries[size_t(m_active)];
    if(!e.edit->isVisible()) return;
    e.edit->setFocus(Qt::OtherFocusReason);
    e.edit->setText(text);
    e.edit->setCursorPosition(int(text.size()));
    onEdited(m_active);
}

bool HeadsUpInput::hasFocus() const {
    return std::any_of(m_entries.begin(), m_entries.end(), [](const Entry &e) { return isFocused(e.edit); });
}

QLineEdit *HeadsUpInput::edit(int i) const { return i >= 0 && i < count() ? m_entries[size_t(i)].edit : nullptr; }

void HeadsUpInput::focusField(int i) {
    if(m_entries.empty()) return;
    m_active = (i % count() + count()) % count();
    QLineEdit *ed = m_entries[size_t(m_active)].edit;
    ed->setFocus(Qt::TabFocusReason);
    ed->selectAll();
}

void HeadsUpInput::onEdited(int i) {
    Entry &e = m_entries[size_t(i)];
    const QString text = e.edit->text().trimmed();
    e.locked = !text.isEmpty();
    e.value.reset();
    if(e.unit) e.unit->setActive(text.isEmpty() || units::isNumber(text));
    if(e.locked && m_eval) {
        const cad::EvalResult r = m_eval(expression(i).toStdString(), e.def.kind);
        if(r.ok) e.value = r.value;
    }
    m_active = i;
    updateStyle(i);
    emit valuesChanged();
}

void HeadsUpInput::updateStyle(int i) {
    const Entry &e = m_entries[size_t(i)];
    const bool invalid = e.locked && !e.value;
    const QString border = invalid ? QStringLiteral("#d23c3c") : e.locked ? QStringLiteral("#1a65c9") : QStringLiteral("#8fb3e3");
    const QString text = invalid ? QStringLiteral("#b02020") : e.locked ? QStringLiteral("#10223c") : QStringLiteral("#3b4552");
    e.edit->setStyleSheet(QString::fromLatin1(kBoxStyle).arg(border, text) + QStringLiteral(" #unitSuffix { border: none; }"));
}

bool HeadsUpInput::eventFilter(QObject *o, QEvent *ev) {
    if(ev->type() != QEvent::KeyPress) return QObject::eventFilter(o, ev);
    auto *k = static_cast<QKeyEvent *>(ev);
    int idx = -1;
    for(int i = 0; i < count(); ++i)
        if(m_entries[size_t(i)].edit == o) idx = i;
    if(idx < 0) return false;
    switch(k->key()) {
    case Qt::Key_Tab:
        focusField(idx + 1);
        return true;
    case Qt::Key_Backtab:
        focusField(idx - 1);
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit commitRequested();
        return true;
    case Qt::Key_Escape:
        emit cancelled();
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------

InlineValueEditor::InlineValueEditor(QWidget *host, const QString &expr, cad::ValueKind kind, QPointF center,
                                     Apply apply)
    : QLineEdit(host), m_apply(std::move(apply)), m_kind(kind) {
    setObjectName(QStringLiteral("dimensionEdit"));
    const QString text = units::displayText(expr, kind);
    setText(text);
    setAlignment(Qt::AlignCenter);
    setStyleSheet(QString::fromLatin1(kBoxStyle).arg(QStringLiteral("#1a65c9"), QStringLiteral("#10223c")) +
                  QStringLiteral(" #unitSuffix { border: none; }"));
    m_unit = UnitSuffix::attach(this, kind);
    const int suffix = m_unit ? m_unit->sizeHint().width() + 2 : 0;
    const int w = std::max(90 + suffix, fontMetrics().horizontalAdvance(text) + 30 + suffix);
    resize(w, sizeHint().height());
    QPoint tl(int(center.x() - w / 2.0), int(center.y() - height() / 2.0));
    tl.setX(std::clamp(tl.x(), 2, std::max(2, host->width() - w - 2)));
    tl.setY(std::clamp(tl.y(), 2, std::max(2, host->height() - height() - 2)));
    move(tl);
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    selectAll();
}

void InlineValueEditor::keyPressEvent(QKeyEvent *e) {
    switch(e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        QString error;
        const QString expr = units::toExpression(text(), m_unit ? m_unit->unit() : QString(), m_kind);
        if(m_apply && m_apply(expr, &error)) {
            finish(true);
        } else {
            setStyleSheet(QString::fromLatin1(kBoxStyle).arg(QStringLiteral("#d23c3c"), QStringLiteral("#b02020")) +
                          QStringLiteral(" #unitSuffix { border: none; }"));
            setToolTip(error);
            QToolTip::showText(mapToGlobal(QPoint(0, height())), error, this);
        }
        return;
    }
    case Qt::Key_Escape:
        finish(false);
        return;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return;
    default:
        QLineEdit::keyPressEvent(e);
    }
}

void InlineValueEditor::focusOutEvent(QFocusEvent *e) {
    QLineEdit::focusOutEvent(e);
    if(e->reason() == Qt::PopupFocusReason) return; // the unit drop-down
    // Let a click on the canvas land before closing.
    QTimer::singleShot(0, this, [this] { finish(false); });
}

void InlineValueEditor::finish(bool applied) {
    if(m_done) return;
    m_done = true;
    QWidget *host = parentWidget();
    hide();
    emit closed(applied);
    if(host) host->setFocus(Qt::OtherFocusReason);
    deleteLater();
}

} // namespace cadjitsu
