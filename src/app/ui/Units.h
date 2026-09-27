#pragma once

#include "expr/Expression.h"

#include <QString>
#include <QStringList>
#include <QToolButton>

class QLineEdit;

namespace cadly {

// Value boxes show just a number; the unit it is in sits beside it in a small
// drop-down (mm, cm, m, in, ft for lengths; deg, rad for angles). A number
// typed in another unit is converted to the default unit (Settings > Units)
// when it is used. Formulas ("d1 * 2", "1 in + 3 mm") are taken as typed.
namespace units {

QStringList choices(cad::ValueKind kind);
QString defaultUnit(cad::ValueKind kind);
void setDefaultLengthUnit(const QString &unit);
// Base units (mm or radians) per unit.
double factor(const QString &unit);
// Whether `text` is a bare number (no unit, no formula).
bool isNumber(const QString &text, double *value = nullptr);
// A number with an optional unit ("12.5 mm", "1 in", "30 deg", "7") split up;
// false for anything else (formulas, parameter names).
bool splitQuantity(const QString &text, double &number, QString &unit);
// A number the way a value box shows it (no trailing zeros).
QString formatNumber(double value);
// What a value box shows for a stored expression: a quantity as a number in
// the default unit, anything else as it is.
QString displayText(const QString &expr, cad::ValueKind kind);
// What a value box's text means: a bare number in `unit`, converted to the
// default unit ("25.4 mm" for 1 in), or the text as it is.
QString toExpression(const QString &text, const QString &unit, cad::ValueKind kind);
// A value in base units, as a number in the default unit.
QString formatInDefault(double base, cad::ValueKind kind, int decimals = -1);

} // namespace units

// The unit drop-down inside the right end of a value box.
class UnitSuffix : public QToolButton {
    Q_OBJECT

public:
    // Attaches one to `edit` (none for plain numbers: returns nullptr).
    static UnitSuffix *attach(QLineEdit *edit, cad::ValueKind kind);

    QString unit() const { return m_unit; }
    // Without emitting unitPicked().
    void setUnit(const QString &unit);
    void resetToDefault() { setUnit(units::defaultUnit(m_kind)); }
    // Dimmed while the box holds a formula (whose units are its own).
    void setActive(bool on);
    QSize sizeHint() const override;

signals:
    void unitPicked(const QString &unit);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

private:
    UnitSuffix(QLineEdit *edit, cad::ValueKind kind);
    void place();

    QLineEdit *m_edit;
    cad::ValueKind m_kind;
    QString m_unit;
    bool m_active = true;
};

} // namespace cadly
