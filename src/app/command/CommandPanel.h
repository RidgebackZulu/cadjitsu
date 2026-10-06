#pragma once

#include "base/Status.h"
#include "expr/Expression.h"

#include <QFrame>
#include <QLineEdit>

#include <functional>
#include <map>
#include <optional>
#include <string>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace cadjitsu {

enum class IconId;
class AngleDial;
class UnitSuffix;

// A value box that shows just a number, with its unit in a drop-down at its
// right end (see units::), and also takes expressions ("d1 * 2", "1 in + 2");
// it shows whether they evaluate.
class ValueField : public QLineEdit {
    Q_OBJECT

public:
    using Evaluator = std::function<cad::EvalResult(const std::string &expr, cad::ValueKind kind)>;

    ValueField(cad::ValueKind kind, Evaluator eval, QWidget *parent = nullptr);
    // Shows a stored expression (a quantity as a number in the default unit),
    // without emitting edited().
    void setExpression(const QString &expr);
    // As if the user had typed `expr` (emits edited()).
    void enterExpression(const QString &expr);
    // As if the user had typed `text` with `unit` chosen (the canvas box).
    void enterInput(const QString &text, const QString &unit);
    // What the box means: a typed number with its unit (converted to the
    // default unit), or the expression as typed.
    QString expression() const;
    // The unit a typed number is in ("" for plain numbers).
    QString unit() const;
    void setUnit(const QString &unit); // as if picked (emits edited())
    std::optional<double> value() const { return m_value; }
    bool valid() const { return m_value.has_value(); }
    cad::ValueKind kind() const { return m_kind; }

signals:
    void edited();
    // The value was worked out again (after any change of text).
    void revalidated();
    void unitChanged(const QString &unit);

protected:
    // Focusing a value, or the first click into one the user has not typed
    // in yet, selects it all, so typing replaces it (as in Fusion).
    void focusInEvent(QFocusEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    void revalidate();

    cad::ValueKind m_kind;
    Evaluator m_eval;
    UnitSuffix *m_unit = nullptr;
    std::optional<double> m_value;
    bool m_selectOnClick = true;
};

// A selection input ("1 selected"): click it to make it the input that
// canvas picks go to; the cross clears it.
class SelectionField : public QFrame {
    Q_OBJECT

public:
    explicit SelectionField(const QString &hint, QWidget *parent = nullptr);
    void setCount(int n);
    int count() const { return m_count; }
    // What is picked, by name ("Y axis"), shown instead of "1 selected".
    void setDetail(const QString &detail);
    QString text() const;
    void setActive(bool on);
    bool active() const { return m_active; }

signals:
    void activated();
    void cleared();

protected:
    void mousePressEvent(QMouseEvent *e) override;

private:
    void restyle();

    QLabel *m_text;
    QToolButton *m_clear;
    QString m_hint, m_detail;
    int m_count = 0;
    bool m_active = false;
};

// Fusion 360-style command dialog shown at the right of the canvas: a title,
// rows of inputs, a message line and OK / Cancel. Enter accepts, Esc cancels.
class CommandPanel : public QFrame {
    Q_OBJECT

public:
    explicit CommandPanel(QWidget *canvas);

    void begin(const QString &title, IconId icon);
    void end();
    bool isOpen() const { return isVisible(); }

    SelectionField *addSelection(const QString &label, const QString &hint, const char *name);
    QComboBox *addChoice(const QString &label, const QStringList &options, const char *name);
    ValueField *addValue(const QString &label, cad::ValueKind kind, ValueField::Evaluator eval, const char *name);
    // An angle value box with a circular dial (in `accent`) beside it; the
    // two follow each other. setRowVisible() takes the value box.
    ValueField *addAngle(const QString &label, ValueField::Evaluator eval, const char *name, const QColor &accent);
    AngleDial *angleDial(ValueField *field) const;
    QCheckBox *addCheck(const QString &label, const char *name);
    // A box for text of several lines: Enter accepts, Shift+Enter starts a new line.
    QPlainTextEdit *addTextBox(const QString &label, const char *name);
    // A read-only value (selectable text) and a push button in the value column.
    QLabel *addInfo(const QString &label, const char *name);
    QPushButton *addButton(const QString &label, const QString &text, const char *name);
    QLabel *addSection(const QString &title);
    void setRowVisible(QWidget *field, bool visible);
    void setRowLabel(QWidget *field, const QString &label);
    void setMessage(const QString &text, cad::Severity severity = cad::Severity::Ok);
    QString message() const;
    void setOkEnabled(bool on);
    QPushButton *okButton() const { return m_ok; }
    QPushButton *cancelButton() const { return m_cancel; }
    void reposition();

signals:
    void accepted();
    void cancelled();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void addRow(const QString &label, QWidget *field);

    QLabel *m_title;
    QLabel *m_titleIcon;
    QWidget *m_body;
    QGridLayout *m_rows;
    QLabel *m_message;
    QPushButton *m_ok, *m_cancel;
    std::map<QWidget *, QLabel *> m_labels;
    std::map<QWidget *, QWidget *> m_rowWidgets; // a field inside a row's widget (angle + dial)
    std::map<ValueField *, AngleDial *> m_dials;
    int m_nextRow = 0;
};

} // namespace cadjitsu
