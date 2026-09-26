#include "TestRegistry.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QtTest>

#include <memory>

std::vector<std::function<QObject *()>> &cadlyTestFactories() {
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
    int failures = 0;
    for(const auto &make : cadlyTestFactories()) {
        std::unique_ptr<QObject> t(make());
        failures += QTest::qExec(t.get(), argc, argv);
    }
    return failures;
}
