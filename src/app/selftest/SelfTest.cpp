#include "selftest/SelfTest.h"

#include "MainWindow.h"
#include "model/ModelView.h"
#include "selftest/DemoModels.h"
#include "selftest/TestUtil.h"
#include "sketch/HeadsUpInput.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "viewport/ViewCube.h"
#include "viewport/Viewport.h"

#include "base/Version.h"
#include "features/SketchFeature.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QKeyEvent>
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

// Keys go to whichever widget of the window has focus (the canvas or a value box).
void sendKey(MainWindow &w, int key, const QString &text = {}) {
    QWidget *target = w.focusWidget() ? w.focusWidget() : w.viewport();
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(target, &release);
}

void typeText(MainWindow &w, const QString &text) {
    for(const QChar c : text) sendKey(w, c.isDigit() ? Qt::Key_0 + c.digitValue() : c.unicode(), QString(c));
}

void clickAt(Viewport *vp, QPointF p) {
    sendMouse(vp, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
    sendMouse(vp, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    sendMouse(vp, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}

// Sketch mode end to end: pick the XY plane, draw a plate outline with typed
// dimensions, a hole, a slot of lines and an arc, dimension it, select for
// statistics, and finish the sketch into the timeline.
bool sketchScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    Viewport *vp = w.viewport();
    SketchMode *mode = w.sketchMode();
    vp->setStandardView(StandardView::Home, false);
    if(!waitForFrames(vp, 2)) return false;
    bool ok = true;
    auto check = [&](bool cond, const QString &what) {
        log << (cond ? "  ok   " : "  FAIL ") << what << "\n";
        ok &= cond;
    };
    auto shot = [&](const char *name) {
        waitForFrames(vp, 2);
        processEventsFor(30);
        w.grab().save(out.filePath(QString::fromLatin1(name)));
    };

    // Create Sketch shows the origin planes; hover then click the XY plane.
    w.action(QStringLiteral("createSketch"))->trigger();
    check(mode->pickingPlane(), QStringLiteral("Create Sketch asks for a plane"));
    const float s = vp->camera().viewHeightAtTarget() * 0.2f;
    const QPointF xy = vp->camera().project(QVector3D(s * 0.85f, s * 0.85f, 0));
    sendMouse(vp, QEvent::MouseMove, xy, Qt::NoButton, Qt::NoButton);
    shot("sketch_pick_plane.png");
    clickAt(vp, xy);
    check(mode->active(), QStringLiteral("clicking the XY plane starts a sketch"));
    if(!mode->active()) return false;
    processEventsFor(450);
    check(vp->camera().forward().z() < -0.999f, QStringLiteral("the view looks at the sketch plane"));
    SketchEditor *ed = mode->editor();
    auto at = [&](double x, double y) { return ed->toScreen({x, y}); };

    // Plate outline: rectangle from the origin, 60 x 40 typed in the heads-up boxes.
    w.action(QStringLiteral("sketchRectangle"))->trigger();
    clickAt(vp, at(0, 0));
    sendMouse(vp, QEvent::MouseMove, at(45, 28), Qt::NoButton, Qt::NoButton);
    typeText(w, QStringLiteral("60"));
    sendKey(w, Qt::Key_Tab);
    typeText(w, QStringLiteral("40"));
    shot("sketch_hud.png");
    sendKey(w, Qt::Key_Return);

    // A 12 mm hole.
    w.action(QStringLiteral("sketchCircle"))->trigger();
    clickAt(vp, at(18, 20));
    sendMouse(vp, QEvent::MouseMove, at(24, 20), Qt::NoButton, Qt::NoButton);
    typeText(w, QStringLiteral("12"));
    sendKey(w, Qt::Key_Return);

    // A slot: two lines and two arcs.
    w.action(QStringLiteral("sketchLine"))->trigger();
    clickAt(vp, at(36, 14));
    clickAt(vp, at(48, 14.2));
    sendKey(w, Qt::Key_Escape);
    clickAt(vp, at(36, 26));
    clickAt(vp, at(48, 26.2));
    sendKey(w, Qt::Key_Escape);
    w.action(QStringLiteral("sketchArc"))->trigger();
    clickAt(vp, at(48, 14));
    clickAt(vp, at(48, 26));
    clickAt(vp, at(54, 20));
    clickAt(vp, at(36, 26));
    clickAt(vp, at(36, 14));
    clickAt(vp, at(30, 20));
    sendKey(w, Qt::Key_Escape);

    // Dimension the hole position from the origin (horizontal) and edit it to 16.
    w.action(QStringLiteral("sketchDimension"))->trigger();
    clickAt(vp, at(0, 0));
    clickAt(vp, at(18, 20));
    clickAt(vp, at(9, -8));
    if(auto *box = mode->dimensionEditor()) {
        box->selectAll();
        typeText(w, QStringLiteral("16"));
        sendKey(w, Qt::Key_Return);
    }
    processEventsFor(30);
    sendKey(w, Qt::Key_Escape);

    // A construction circle around the hole, sharing its centre (X toggles construction).
    w.action(QStringLiteral("sketchCircle"))->trigger();
    clickAt(vp, at(16, 20));
    sendMouse(vp, QEvent::MouseMove, at(26, 20), Qt::NoButton, Qt::NoButton);
    typeText(w, QStringLiteral("22"));
    sendKey(w, Qt::Key_Return);
    sendKey(w, Qt::Key_Escape);
    clickAt(vp, at(16, 31));
    w.action(QStringLiteral("sketchConstruction"))->trigger();
    int construction = 0;
    for(const auto &e : ed->sketch().entities) construction += e.construction && e.type == cad::SkType::Circle;
    check(construction == 1, QStringLiteral("X turns the selected circle into construction geometry"));

    const cad::Sketch &sk = ed->sketch();
    int lines = 0, circles = 0, arcs = 0, dims = 0;
    for(const auto &e : sk.entities) {
        lines += e.type == cad::SkType::Line;
        circles += e.type == cad::SkType::Circle;
        arcs += e.type == cad::SkType::Arc;
    }
    for(const auto &c : sk.constraints) dims += cad::isDimension(c.type) ? 1 : 0;
    log << "  sketch: " << lines << " lines, " << circles << " circles, " << arcs << " arcs, " << dims
        << " dimensions, " << ed->profiles().size() << " profiles, dof " << ed->solveResult().dof << "\n";
    check(lines == 6 && circles == 2 && arcs == 2, QStringLiteral("all curves were drawn"));
    check(dims == 5, QStringLiteral("typed values became dimensions"));
    check(ed->profiles().size() == 3, QStringLiteral("plate, hole and slot profiles"));
    const cad::Profile *plate = nullptr;
    for(const auto &p : ed->profiles())
        if(!plate || std::fabs(p.area) > std::fabs(plate->area)) plate = &p;
    const double slotArea = 12 * 12 + cad::kPi * 36;
    const double expect = 60 * 40 - cad::kPi * 36 - slotArea;
    check(plate && std::fabs(std::fabs(plate->area) - expect) < 0.5,
          QStringLiteral("plate profile area %1 (expected %2)").arg(plate ? std::fabs(plate->area) : 0).arg(expect));
    bool holeAt16 = false;
    for(const auto &e : sk.entities)
        if(e.type == cad::SkType::Circle && !e.construction) holeAt16 = std::fabs(sk.pointPos(e.a).x - 16.0) < 1e-6;
    check(holeAt16, QStringLiteral("editing the dimension moved the hole to x = 16"));

    // Select the bottom edge: statistics at the bottom right.
    clickAt(vp, at(30, 0));
    const QString stats = w.selectionStatsLabel()->text();
    log << "  stats: " << stats << "\n";
    check(stats.contains(QStringLiteral("Length")) && stats.contains(QStringLiteral("60.00")),
          QStringLiteral("selecting a line shows its length"));
    shot("sketch_done.png");

    // Finish: the sketch lands in the timeline with its profiles.
    w.action(QStringLiteral("finishSketch"))->trigger();
    check(!mode->active(), QStringLiteral("Finish Sketch leaves sketch mode"));
    const auto &features = w.document().features();
    auto sf = features.empty() ? nullptr : std::dynamic_pointer_cast<const cad::SketchFeature>(features.back());
    check(sf != nullptr, QStringLiteral("the sketch is in the timeline"));
    if(sf) {
        const auto st = w.document().displayedState();
        check(st->sketches.count(sf->id) && st->sketches.at(sf->id)->profiles.size() == 3,
              QStringLiteral("the committed sketch has the same profiles"));
    }
    vp->setStandardView(StandardView::Home, false);
    shot("sketch_finished.png");
    return ok;
}

const std::map<QString, Scenario> &scenarios() {
    static const std::map<QString, Scenario> s = {
        {QStringLiteral("smoke"), smokeScenario},
        {QStringLiteral("views"), viewsScenario},
        {QStringLiteral("sketch"), sketchScenario},
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
