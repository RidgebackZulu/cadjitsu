// Application-level tests: drive the real MainWindow under a (virtual) display.
#include "MainWindow.h"
#include "selftest/TestUtil.h"
#include "viewport/Viewport.h"

#include <QSurfaceFormat>
#include <QtTest>

using namespace cadly;

class AppTests : public QObject {
    Q_OBJECT

private slots:
    void viewportRendersBackgroundGradient() {
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(waitForFrames(w.viewport(), 2));

        const QImage img = w.viewport()->grabFramebuffer();
        QVERIFY(!img.isNull());
        const QColor top = img.pixelColor(img.width() / 2, 2);
        const QColor bottom = img.pixelColor(img.width() / 2, img.height() - 3);
        QVERIFY2(colorNear(top, w.viewport()->backgroundTop(), 6), qPrintable(top.name()));
        QVERIFY2(colorNear(bottom, w.viewport()->backgroundBottom(), 6), qPrintable(bottom.name()));
    }
};

int main(int argc, char **argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    AppTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_app.moc"
