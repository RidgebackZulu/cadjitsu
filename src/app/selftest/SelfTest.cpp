#include "selftest/SelfTest.h"

#include "MainWindow.h"
#include "selftest/TestUtil.h"
#include "viewport/Viewport.h"

#include "base/Version.h"

#include <QDir>
#include <QImage>
#include <QTextStream>

#include <functional>
#include <map>

namespace cadly {

namespace {

using Scenario = std::function<bool(MainWindow &, const QDir &, QTextStream &)>;

// Window shows, the viewport renders the background gradient, and the frame
// can be read back.
bool smokeScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    if(!waitForFrames(w.viewport(), 2)) {
        log << "viewport never produced a frame\n";
        return false;
    }
    const QImage img = w.viewport()->grabFramebuffer();
    img.save(out.filePath(QStringLiteral("smoke.png")));
    log << "backend: " << w.viewport()->backendName() << ", frame " << img.width() << "x"
        << img.height() << "\n";
    if(img.isNull()) return false;

    const QColor top = img.pixelColor(img.width() / 2, 2);
    const QColor bottom = img.pixelColor(img.width() / 2, img.height() - 3);
    log << "top " << top.name() << " bottom " << bottom.name() << "\n";
    return colorNear(top, w.viewport()->backgroundTop(), 6) &&
           colorNear(bottom, w.viewport()->backgroundBottom(), 6);
}

const std::map<QString, Scenario> &scenarios() {
    static const std::map<QString, Scenario> s = {
        {QStringLiteral("smoke"), smokeScenario},
    };
    return s;
}

} // namespace

QStringList selfTestNames() {
    QStringList names;
    for(const auto &kv : scenarios()) names << kv.first;
    return names;
}

int runSelfTest(MainWindow &window, const QString &name, const QString &outDir) {
    QTextStream log(stderr);
    auto it = scenarios().find(name);
    if(it == scenarios().end()) {
        log << "unknown selftest '" << name << "'; available: " << selfTestNames().join(", ")
            << "\n";
        return 2;
    }
    QDir out(outDir.isEmpty() ? QDir::currentPath() : outDir);
    if(!out.exists()) out.mkpath(QStringLiteral("."));

    log << "Cadly " << cad::version() << " (OCCT " << QString::fromStdString(cad::occtVersion())
        << ") selftest '" << name << "'\n";
    const bool ok = it->second(window, out, log);
    log << "selftest '" << name << "': " << (ok ? "PASS" : "FAIL") << "\n";
    log.flush();
    return ok ? 0 : 1;
}

} // namespace cadly
