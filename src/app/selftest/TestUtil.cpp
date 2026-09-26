#include "selftest/TestUtil.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRhiWidget>
#include <QTimer>

#include <cstdlib>

namespace cadly {

bool waitForFrames(QRhiWidget *widget, int frames, int timeoutMs) {
    int submitted = 0;
    bool failed = false;
    QEventLoop loop;
    auto c1 = QObject::connect(widget, &QRhiWidget::frameSubmitted, &loop, [&] {
        if(++submitted >= frames) loop.quit();
        else widget->update();
    });
    auto c2 = QObject::connect(widget, &QRhiWidget::renderFailed, &loop, [&] {
        failed = true;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    widget->update();
    loop.exec();
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    return !failed && submitted >= frames;
}

void processEventsFor(int ms) {
    QElapsedTimer t;
    t.start();
    while(t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

bool colorNear(const QColor &a, const QColor &b, int tolerance) {
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

} // namespace cadly
