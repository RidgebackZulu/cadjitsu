#include "command/ThreadOptions.h"

#include "features/ThreadTable.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSettings>

#include <cmath>

namespace cadjitsu {

namespace {
const char *kRecentKey = "threads/recent";
constexpr int kRecent = 5;
const double kAngles[] = {82, 90, 100, 120};
} // namespace

QStringList recentThreadSizes() {
    QStringList out;
    for(const QString &s : QSettings().value(kRecentKey).toStringList())
        if(cad::findThread(s.toStdString()) && !out.contains(s) && out.size() < kRecent) out << s;
    return out;
}

void noteThreadSize(const QString &name) {
    if(name.isEmpty() || !cad::findThread(name.toStdString())) return;
    QStringList list = recentThreadSizes();
    list.removeAll(name);
    list.prepend(name);
    while(list.size() > kRecent) list.removeLast();
    QSettings().setValue(kRecentKey, list);
}

void fillThreadSizes(QComboBox *box, const QString &first) {
    box->clear();
    if(!first.isEmpty()) box->addItem(first);
    const QStringList recent = recentThreadSizes();
    for(const QString &s : recent) box->addItem(s);
    if(!recent.isEmpty()) box->insertSeparator(box->count());
    for(const cad::ThreadSpec &t : cad::threadTable()) box->addItem(QString::fromStdString(t.name));
}

void CountersinkRows::add(CommandPanel &panel, ValueField::Evaluator eval, const char *prefix) {
    const QByteArray p(prefix);
    m_on = panel.addCheck(QObject::tr("Countersink"), p + "Countersink");
    m_on->setChecked(true);
    m_on->setToolTip(QObject::tr("A countersink at the hole's open ends, so a screw starts true."));
    QStringList angles;
    for(double a : kAngles) angles << QStringLiteral("%1°").arg(a);
    m_angle = panel.addChoice(QObject::tr("Countersink Angle"), angles, p + "CountersinkAngle");
    m_angle->setCurrentIndex(1);
    m_angle->setToolTip(QObject::tr("Included angle: 90° metric screws, 82° inch screws, 100° and 120° for sheet "
                                    "and plastics."));
    m_diameter = panel.addValue(QObject::tr("Countersink Diameter"), cad::ValueKind::Length, std::move(eval),
                                p + "CountersinkDiameter");
    m_diameter->setToolTip(QObject::tr("At the face. Leave it blank for a little over the thread's outside."));
    QObject::connect(m_on, &QCheckBox::toggled, m_angle, [this](bool on) {
        m_angle->setEnabled(on);
        m_diameter->setEnabled(on);
    });
}

void CountersinkRows::load(bool on, double angle, const cad::ParamSlot &diameter) {
    m_on->setChecked(on);
    int best = 1;
    for(int i = 0; i < 4; ++i)
        if(std::fabs(kAngles[i] - angle) < std::fabs(kAngles[best] - angle)) best = i;
    m_angle->setCurrentIndex(best);
    m_diameter->setExpression(QString::fromStdString(diameter.expr));
}

void CountersinkRows::setVisible(CommandPanel &panel, bool visible) {
    panel.setRowVisible(m_on, visible);
    panel.setRowVisible(m_angle, visible);
    panel.setRowVisible(m_diameter, visible);
}

void CountersinkRows::setAutoDiameter(double mm) {
    m_diameter->setPlaceholderText(QObject::tr("auto (%1)").arg(mm, 0, 'f', 2));
}

bool CountersinkRows::valid() const { return !m_on->isChecked() || m_diameter->text().trimmed().isEmpty() || m_diameter->valid(); }

bool CountersinkRows::on() const { return m_on->isChecked(); }

double CountersinkRows::angle() const { return kAngles[std::clamp(m_angle->currentIndex(), 0, 3)]; }

cad::ParamSlot CountersinkRows::diameter(const cad::ParamSlot &slot) const {
    if(m_diameter->text().trimmed().isEmpty()) return {};
    return {slot.name, m_diameter->expression().toStdString()};
}

} // namespace cadjitsu
