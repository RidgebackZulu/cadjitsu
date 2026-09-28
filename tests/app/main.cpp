#include "TestRegistry.h"
#include "ui/Fonts.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QtTest>

#include <memory>

std::vector<std::function<QObject *()>> &cadjitsuTestFactories() {
    static std::vector<std::function<QObject *()>> f;
    return f;
}

int main(int argc, char **argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    cadjitsu::applyLightTheme(app);
    // Settings the tests touch stay out of the user's own Cadjitsu settings.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Cadjitsu Tests"));
    cadjitsu::registerBundledFonts();
    // CADJITSU_TEST_CLASS=SketchTests runs one test class (other arguments go to QtTest).
    const QByteArray only = qgetenv("CADJITSU_TEST_CLASS");
    int failures = 0;
    for(const auto &make : cadjitsuTestFactories()) {
        std::unique_ptr<QObject> t(make());
        if(!only.isEmpty() && only != t->metaObject()->className()) continue;
        failures += QTest::qExec(t.get(), argc, argv);
    }
    return failures;
}
