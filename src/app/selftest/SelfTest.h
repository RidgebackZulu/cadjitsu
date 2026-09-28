#pragma once

#include <QString>
#include <QStringList>

namespace cadjitsu {

class MainWindow;

// Scripted end-to-end scenarios run with `Cadjitsu --selftest=<name> --out <dir>`.
// Each drives the real UI and command layer, saves screenshots into the output
// directory, and returns the process exit code (0 = pass).
QStringList selfTestNames();
int runSelfTest(MainWindow &window, const QString &name, const QString &outDir);

} // namespace cadjitsu
