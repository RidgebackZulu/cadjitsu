#include "selftest/SelfTest.h"

#include "MainWindow.h"
#include "model/ModelView.h"
#include "selftest/DemoModels.h"
#include "selftest/TestUtil.h"
#include "viewport/ViewCube.h"
#include "viewport/Viewport.h"

#include "base/Version.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
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

QColor expectedBackground(const Viewport *vp, const QImage &img, int y) {
    const double t = double(y) / std::max(1, img.height() - 1);
    const QColor a = vp->backgroundTop(), b = vp->backgroundBottom();
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

// Logical-pixel position -> framebuffer pixel.
QPoint toImage(const Viewport *vp, const QImage &img, QPointF p) {
    return QPoint(int(p.x() * img.width() / vp->width()), int(p.y() * img.height() / vp->height()));
}

void sendMouse(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, mods);
    QCoreApplication::sendEvent(w, &ev);
}

// A modelled part renders in every visual style, hover highlights the face
// under the cursor, clicking selects it with statistics, and the ViewCube
// switches to the top view.
bool viewsScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    buildDemoBracket(w.document());
    w.refresh();
    Viewport *vp = w.viewport();
    vp->setStandardView(StandardView::Home, false);
    if(!waitForFrames(vp, 3)) {
        log << "no frames\n";
        return false;
    }
    bool ok = true;
    auto check = [&](bool cond, const QString &what) {
        log << (cond ? "  ok   " : "  FAIL ") << what << "\n";
        ok &= cond;
    };

    QImage img = vp->grabFramebuffer();
    img.save(out.filePath(QStringLiteral("views_home.png")));
    const QPoint c(img.width() / 2, img.height() / 2);
    check(!colorNear(img.pixelColor(c), expectedBackground(vp, img, c.y()), 12), QStringLiteral("part visible at centre"));

    // Hover the top face of the boss.
    const QPointF bossTop = vp->camera().project(QVector3D(45, 25.5f, 22)); // on the boss, outside its hole
    sendMouse(vp, QEvent::MouseMove, bossTop, Qt::NoButton, Qt::NoButton);
    waitForFrames(vp, 2);
    check(vp->hovered().kind == PickHit::Kind::Face, QStringLiteral("hover finds a face"));
    img = vp->grabFramebuffer();
    img.save(out.filePath(QStringLiteral("views_hover.png")));
    const QColor hc = img.pixelColor(toImage(vp, img, bossTop));
    check(hc.blue() > hc.red() + 25, QStringLiteral("hovered face is highlighted blue (%1)").arg(hc.name()));

    // Click selects it and shows statistics.
    sendMouse(vp, QEvent::MouseButtonPress, bossTop, Qt::LeftButton, Qt::LeftButton);
    sendMouse(vp, QEvent::MouseButtonRelease, bossTop, Qt::LeftButton, Qt::NoButton);
    processEventsFor(50);
    const QString stats = w.selectionStatsLabel()->text();
    log << "  stats: " << stats << "\n";
    check(stats.contains(QStringLiteral("Plane")) && stats.contains(QStringLiteral("Area")),
          QStringLiteral("selection statistics shown"));

    const std::pair<DisplayStyle, const char *> styles[] = {{DisplayStyle::Shaded, "views_shaded.png"},
                                                            {DisplayStyle::Wireframe, "views_wireframe.png"},
                                                            {DisplayStyle::ShadedWithEdges, "views_edges.png"}};
    for(const auto &[style, file] : styles) {
        vp->setDisplayStyle(style);
        waitForFrames(vp, 2);
        vp->grabFramebuffer().save(out.filePath(QString::fromLatin1(file)));
    }

    // Click the TOP face of the ViewCube.
    const QRect cube = ViewCube::rect(vp->size());
    const QMatrix4x4 m = ViewCube::viewProjection(vp->camera().rotation);
    const QVector4D clip = m * QVector4D(0, 0, 1, 1);
    const QPointF topPx(cube.left() + (clip.x() / clip.w() + 1) * 0.5 * cube.width(),
                        cube.top() + (1 - clip.y() / clip.w()) * 0.5 * cube.height());
    sendMouse(vp, QEvent::MouseButtonPress, topPx, Qt::LeftButton, Qt::LeftButton);
    sendMouse(vp, QEvent::MouseButtonRelease, topPx, Qt::LeftButton, Qt::NoButton);
    processEventsFor(700);
    const QVector3D f = vp->camera().forward();
    check(f.z() < -0.999f, QStringLiteral("ViewCube TOP gives the top view (forward %1 %2 %3)").arg(f.x()).arg(f.y()).arg(f.z()));
    waitForFrames(vp, 2);
    vp->grabFramebuffer().save(out.filePath(QStringLiteral("views_top.png")));
    vp->setStandardView(StandardView::Home, false);
    waitForFrames(vp, 2);
    w.grab().save(out.filePath(QStringLiteral("views_window.png")));
    return ok;
}

const std::map<QString, Scenario> &scenarios() {
    static const std::map<QString, Scenario> s = {
        {QStringLiteral("smoke"), smokeScenario},
        {QStringLiteral("views"), viewsScenario},
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
