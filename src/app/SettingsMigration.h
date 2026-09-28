#pragma once

class QSettings;

namespace cadjitsu {

// The app was called Cadly before it became Cadjitsu. The first time
// Cadjitsu runs with no settings of its own, it takes over Cadly's (mouse
// bindings, units, MCP port and token...). Returns whether it copied any.
bool migrateSettingsFromCadly();

// Copies every key of `from` into `to` if `to` has none yet.
bool copySettingsIfEmpty(QSettings &from, QSettings &to);

} // namespace cadjitsu
