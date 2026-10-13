#pragma once

#include "command/CommandPanel.h"

#include "doc/Feature.h"

#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;

namespace cadjitsu {

// The thread sizes used last (newest first, at most five), kept between runs.
QStringList recentThreadSizes();
void noteThreadSize(const QString &name);
// Fills a thread size choice: `first` (if not empty), the recent sizes, a
// line, then every size in the table.
void fillThreadSizes(QComboBox *box, const QString &first = {});

// A threaded hole's countersink rows: on / off, the included angle and the
// diameter at the face (blank: a little over the thread's major diameter).
class CountersinkRows {
public:
    void add(CommandPanel &panel, ValueField::Evaluator eval, const char *prefix);
    void load(bool on, double angle, const cad::ParamSlot &diameter);
    void setVisible(CommandPanel &panel, bool visible);
    // The auto diameter, shown greyed in the blank diameter box.
    void setAutoDiameter(double mm);
    bool valid() const; // the diameter is blank or a value
    bool on() const;
    double angle() const;
    // The diameter as a parameter (empty: auto); `slot` names it.
    cad::ParamSlot diameter(const cad::ParamSlot &slot) const;

    QCheckBox *onBox() const { return m_on; }
    QComboBox *angleBox() const { return m_angle; }
    ValueField *diameterField() const { return m_diameter; }

private:
    QCheckBox *m_on = nullptr;
    QComboBox *m_angle = nullptr;
    ValueField *m_diameter = nullptr;
};

} // namespace cadjitsu
