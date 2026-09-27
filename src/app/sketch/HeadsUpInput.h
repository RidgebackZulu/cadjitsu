#pragma once

#include "expr/Expression.h"

#include <QLineEdit>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cadly {

class UnitSuffix;

// Fusion 360-style heads-up value boxes that follow the cursor while drawing
// (length and angle of a line, width and height of a rectangle, a circle's
// diameter). They show live values until the user types: a typed value or
// expression locks that quantity until the geometry is committed. Tab moves
// between boxes, Enter commits, Esc gives up.
class HeadsUpInput : public QObject {
    Q_OBJECT

public:
    using Evaluator = std::function<cad::EvalResult(const std::string &expr, cad::ValueKind kind)>;

    struct Field {
        QString name;
        cad::ValueKind kind = cad::ValueKind::Length;
    };

    HeadsUpInput(QWidget *host, Evaluator eval, QObject *parent = nullptr);
    ~HeadsUpInput() override;

    // Sets the boxes for the active tool (hidden until placed).
    void setFields(const std::vector<Field> &fields);
    void hide();
    bool visible() const;
    int count() const { return int(m_entries.size()); }

    // Shows a live value (base units) while the box is not locked.
    void setLive(int i, double value);
    // Centres box `i` on a canvas position (logical pixels) and shows it.
    void place(int i, QPointF px);
    bool locked(int i) const;
    std::optional<double> value(int i) const; // the typed value, if locked and valid
    QString expression(int i) const;          // what was typed (a number with its unit, or a formula)
    void unlockAll();
    // Starts typing into the active box (a key typed on the canvas).
    void beginTyping(const QString &text);
    bool hasFocus() const;
    QLineEdit *edit(int i) const;
    int activeField() const { return m_active; }

    static QString formatLive(double value, cad::ValueKind kind);

signals:
    void commitRequested();
    void cancelled();
    void valuesChanged();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct Entry {
        Field def;
        QPointer<QLineEdit> edit; // a child of the host, which may be deleted first
        QPointer<UnitSuffix> unit;
        bool locked = false;
        std::optional<double> value;
    };
    void focusField(int i);
    void updateStyle(int i);
    void onEdited(int i);

    QWidget *m_host;
    Evaluator m_eval;
    std::vector<Entry> m_entries;
    int m_active = 0;
};

// A one-shot value box over the canvas, used to type a dimension's value.
// Enter applies (staying open with the error shown if the value is refused),
// Esc or clicking elsewhere closes it.
class InlineValueEditor : public QLineEdit {
    Q_OBJECT

public:
    using Apply = std::function<bool(const QString &text, QString *error)>;

    // Shows `expr` as a number in the default unit (with a unit drop-down);
    // `apply` gets the expression it stands for.
    InlineValueEditor(QWidget *host, const QString &expr, cad::ValueKind kind, QPointF center, Apply apply);
    // Closes without applying.
    void dismiss() { finish(false); }

signals:
    void closed(bool applied);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    void finish(bool applied);

    Apply m_apply;
    cad::ValueKind m_kind;
    QPointer<UnitSuffix> m_unit;
    bool m_done = false;
};

} // namespace cadly
