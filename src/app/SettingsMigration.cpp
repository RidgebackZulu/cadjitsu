#include "SettingsMigration.h"

#include <QSettings>

namespace cadjitsu {

bool copySettingsIfEmpty(QSettings &from, QSettings &to) {
    if(!to.allKeys().isEmpty()) return false;
    const QStringList keys = from.allKeys();
    if(keys.isEmpty()) return false;
    for(const QString &k : keys) to.setValue(k, from.value(k));
    to.sync();
    return true;
}

bool migrateSettingsFromCadly() {
    QSettings current; // Cadjitsu / Cadjitsu
    QSettings old(QStringLiteral("Cadly"), QStringLiteral("Cadly"));
    return copySettingsIfEmpty(old, current);
}

} // namespace cadjitsu
