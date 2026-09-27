#include "selftest/SelfTest.h"

#include "MainWindow.h"
#include "command/Command.h"
#include "command/CommandPanel.h"
#include "command/CombineCommand.h"
#include "command/EdgeCommands.h"
#include "command/ExtrudeCommand.h"
#include "command/HoleCommand.h"
#include "command/MeasureCommand.h"
#include "command/OverhangCommand.h"
#include "command/Manipulator.h"
#include "command/PlaneCommand.h"
#include "ui/AngleDial.h"
#include "command/SectionCommand.h"
#include "command/SplitCommand.h"
#include "model/ModelView.h"
#include "selftest/DemoModels.h"
#include "selftest/TestUtil.h"
#include "sketch/HeadsUpInput.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "mcp/McpButton.h"
#include "mcp/McpDialog.h"
#include "mcp/McpLog.h"
#include "mcp/McpServer.h"
#include "ui/BrowserTree.h"
#include "ui/ExportDialog.h"
#include "ui/Icons.h"
#include "ui/MarkingMenu.h"
#include "ui/Ribbon.h"
#include "ui/TimelineWidget.h"
#include "viewport/ViewCube.h"
#include "viewport/Viewport.h"

#include "base/Version.h"
#include "doc/Section.h"
#include "features/ExtrudeFeature.h"
#include "features/FilletFeature.h"
#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"
#include "io/StepIO.h"
#include "io/StlWriter.h"
#include "mesh/MeshValidator.h"
#include "topo/Resolver.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTcpServer>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>

#include <gp_Pln.hxx>

#include <array>
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
                                                            {DisplayStyle::Rendered, "views_rendered.png"},
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

// How long the event loop goes without running: the longest gap between the
// ticks of a fast timer.
class StallMeter {
public:
    StallMeter() {
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setInterval(2);
        QObject::connect(&m_timer, &QTimer::timeout, [this] { tick(); });
    }
    void start() {
        m_longest = 0;
        m_clock.start();
        m_last = 0;
        m_timer.start();
    }
    double stop() {
        tick();
        m_timer.stop();
        return m_longest;
    }

private:
    void tick() {
        const qint64 now = m_clock.nsecsElapsed();
        m_longest = std::max(m_longest, double(now - m_last) / 1e6);
        m_last = now;
    }

    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_last = 0;
    double m_longest = 0;
};

double modelVolume(const cad::StatePtr &s) {
    double v = 0;
    if(s)
        for(const auto &kv : s->bodies) v += cad::volumeOf(kv.second->shape.shape());
    return v;
}

void doubleClickAt(QWidget *w, QPointF p) {
    sendMouse(w, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
    sendMouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    sendMouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    sendMouse(w, QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
    sendMouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}

void dragAt(QWidget *w, QPointF from, QPointF to) {
    sendMouse(w, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
    sendMouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for(int i = 1; i <= 12; ++i)
        sendMouse(w, QEvent::MouseMove, from + (to - from) * (i / 12.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(w, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

// The M4 acceptance scenario, all through the UI: sketch a rectangle, extrude
// it 20 mm, sketch a circle on its top face and cut it through all, then edit
// the first dimension. Checks the volume at every history marker position,
// suppress / unsuppress, that Edit Feature reopens the same dialog with the
// same values and previews live, that undo returns to an empty design, and
// that the UI never stalls for more than 50 ms while the model recomputes.
bool plateScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    Viewport *vp = w.viewport();
    SketchMode *mode = w.sketchMode();
    cad::Document &doc = w.document();
    TimelineWidget *tl = w.timeline();
    vp->setStandardView(StandardView::Home, false);
    if(!waitForFrames(vp, 2)) return false;
    bool ok = true;
    auto check = [&](bool cond, const QString &what) {
        log << (cond ? "  ok   " : "  FAIL ") << what << "\n";
        log.flush();
        ok &= cond;
    };
    auto shot = [&](const char *name) {
        waitForFrames(vp, 2);
        processEventsFor(30);
        w.grab().save(out.filePath(QString::fromLatin1(name)));
    };
    auto shown = [&] { return modelVolume(w.modelView()->state()); };
    auto near = [](double a, double b) { return std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(b)); };
    auto vol = [](double v) { return QString::number(v, 'f', 2); };

    // Every wait for the model measures how long the UI thread was kept from
    // running meanwhile. Canvas repaints wait until the model is there: they
    // are not part of recomputing (under software OpenGL one frame of the
    // drilled plate takes ~35 ms by itself), and are timed separately.
    StallMeter meter;
    double worstStall = 0, slowestModel = 0, slowestFrame = 0;
    auto settle = [&](const QString &what) {
        QElapsedTimer t;
        t.start();
        vp->setUpdatesEnabled(false);
        meter.start();
        const bool done = w.waitForModel(60000);
        const double stall = meter.stop();
        const double ms = double(t.nsecsElapsed()) / 1e6;
        vp->setUpdatesEnabled(true);
        t.restart();
        waitForFrames(vp, 1);
        const double frame = double(t.nsecsElapsed()) / 1e6;
        worstStall = std::max(worstStall, stall);
        slowestModel = std::max(slowestModel, ms);
        slowestFrame = std::max(slowestFrame, frame);
        log << QStringLiteral("         %1: model after %2 ms, longest UI stall %3 ms; frame %4 ms\n")
                   .arg(what)
                   .arg(ms, 0, 'f', 1)
                   .arg(stall, 0, 'f', 1)
                   .arg(frame, 0, 'f', 1);
        if(!done) check(false, what + QStringLiteral(": the model never arrived"));
    };
    auto extrudeCommand = [&] { return qobject_cast<ExtrudeCommand *>(w.commands()->command()); };
    const double plate = 60.0 * 40.0 * 20.0, hole = cad::kPi * 25.0;

    // 1. Sketch a 60 x 40 rectangle on the XY plane.
    w.action(QStringLiteral("createSketch"))->trigger();
    const float s = vp->camera().viewHeightAtTarget() * 0.2f;
    clickAt(vp, vp->camera().project(QVector3D(s * 0.85f, s * 0.85f, 0)));
    check(mode->active(), QStringLiteral("Create Sketch on the XY plane"));
    if(!mode->active()) return false;
    processEventsFor(450);
    SketchEditor *ed = mode->editor();
    w.action(QStringLiteral("sketchRectangle"))->trigger();
    clickAt(vp, ed->toScreen({0, 0}));
    sendMouse(vp, QEvent::MouseMove, ed->toScreen({45, 28}), Qt::NoButton, Qt::NoButton);
    typeText(w, QStringLiteral("60"));
    sendKey(w, Qt::Key_Tab);
    typeText(w, QStringLiteral("40"));
    sendKey(w, Qt::Key_Return);
    check(ed->profiles().size() == 1 && std::fabs(std::fabs(ed->profiles().front().area) - 2400.0) < 1e-6,
          QStringLiteral("a 60 x 40 rectangle with typed dimensions"));
    shot("plate_1_sketch.png");

    // 2. E finishes the sketch and extrudes its profile; type 20 and press Enter.
    w.action(QStringLiteral("extrude"))->trigger();
    ExtrudeCommand *ex = extrudeCommand();
    check(!mode->active() && ex && ex->profileCount() == 1,
          QStringLiteral("E finishes the sketch and extrudes its profile"));
    if(!ex) return false;
    settle(QStringLiteral("default preview"));
    check(w.focusWidget() == ex->distanceField(), QStringLiteral("the distance box has the keyboard"));
    typeText(w, QStringLiteral("20"));
    settle(QStringLiteral("20 mm preview"));
    check(w.modelView()->evaluation()->preview && near(shown(), plate) && doc.features().size() == 1,
          QStringLiteral("the preview shows the 20 mm plate (%1) before it is committed").arg(vol(shown())));
    shot("plate_2_extrude_preview.png");
    sendKey(w, Qt::Key_Return);
    settle(QStringLiteral("commit Extrude1"));
    check(!w.commands()->active() && doc.features().size() == 2 && near(shown(), plate),
          QStringLiteral("Enter commits Extrude1: %1 mm3").arg(vol(shown())));
    check(QString::fromStdString(doc.undoLabel()) == QStringLiteral("Create Extrude1"),
          QStringLiteral("as one undo step"));

    // 3. A 10 mm circle on the top face.
    vp->setStandardView(StandardView::Home, false);
    vp->fitAll(false);
    waitForFrames(vp, 1);
    w.action(QStringLiteral("createSketch"))->trigger();
    clickAt(vp, vp->camera().project(QVector3D(30, 20, 20)));
    check(mode->active(), QStringLiteral("Create Sketch on the plate's top face"));
    if(!mode->active()) return false;
    processEventsFor(450);
    ed = mode->editor();
    const std::optional<cad::Vec2> centre = ed->toSketch(vp->camera().project(QVector3D(30, 20, 20)));
    check(centre.has_value() && std::fabs(ed->toWorld(*centre).z() - 20.0f) < 1e-3f,
          QStringLiteral("the sketch lies on the top face"));
    if(!centre) return false;
    w.action(QStringLiteral("sketchCircle"))->trigger();
    clickAt(vp, ed->toScreen(*centre));
    sendMouse(vp, QEvent::MouseMove, ed->toScreen({centre->x + 4, centre->y}), Qt::NoButton, Qt::NoButton);
    typeText(w, QStringLiteral("10"));
    sendKey(w, Qt::Key_Return);
    check(ed->profiles().size() == 1, QStringLiteral("a 10 mm circle"));

    // 4. Extrude it: a new body at first; dragged into the plate and set to
    // Cut it drills the plate; All carries on through it.
    w.action(QStringLiteral("extrude"))->trigger();
    ex = extrudeCommand();
    if(!ex) return false;
    settle(QStringLiteral("hole preview"));
    check(ex->operation() == cad::BodyOperation::NewBody, QStringLiteral("an extrude makes a new body by default"));
    // From the sketch's view the arrow points at the eye: look from the side first.
    vp->setStandardView(StandardView::Home, false);
    waitForFrames(vp, 1);
    const QPointF head = ex->arrow().headOnScreen();
    const QVector3D down = ex->arrow().origin() - ex->arrow().direction() * 8.0f;
    dragAt(vp, head, vp->camera().project(down));
    ex->operationBox()->setCurrentIndex(1); // Operation: Cut
    emit ex->operationBox()->activated(1);
    settle(QStringLiteral("arrow dragged into the plate"));
    check(ex->operation() == cad::BodyOperation::Cut && ex->distanceField()->value().value_or(0) < -7.9,
          QStringLiteral("dragged into the plate and set to Cut (%1)").arg(ex->distanceField()->expression()));
    ex->extentBox()->setCurrentIndex(2); // All
    check(ex->flipBox()->isChecked(), QStringLiteral("All keeps going into the plate"));
    settle(QStringLiteral("cut through all preview"));
    check(near(shown(), plate - hole * 20.0), QStringLiteral("cut through all: %1 mm3").arg(vol(shown())));
    shot("plate_3_cut_preview.png");
    w.commandPanel()->okButton()->click();
    settle(QStringLiteral("commit Extrude2"));
    check(doc.features().size() == 4 && near(shown(), plate - hole * 20.0),
          QStringLiteral("OK commits the cut: %1 mm3").arg(vol(shown())));
    if(doc.features().size() != 4) return false;

    // 5. Scrub the history marker from the start to the end.
    const double atMarker[] = {0.0, 0.0, plate, plate, plate - hole * 20.0};
    tl->findChild<QToolButton *>(QStringLiteral("timelineFirst"))->click();
    for(int m = 0; m <= 4; ++m) {
        if(m > 0) tl->findChild<QToolButton *>(QStringLiteral("timelineForward"))->click();
        settle(QStringLiteral("marker at %1").arg(m));
        check(doc.marker() == m && near(shown(), atMarker[m]),
              QStringLiteral("history marker at %1: %2 mm3").arg(m).arg(vol(shown())));
        if(m == 3) shot("plate_4_scrubbed.png");
    }

    // 6. Suppress the cut and unsuppress it again (from the cache).
    const cad::FeatureId cut = doc.features()[3]->id;
    const size_t computed = w.recompute()->computedFeatures();
    doc.setSuppressed(cut, true);
    settle(QStringLiteral("suppressed"));
    check(near(shown(), plate), QStringLiteral("suppressing the cut removes the hole: %1 mm3").arg(vol(shown())));
    shot("plate_5_suppressed.png");
    doc.setSuppressed(cut, false);
    settle(QStringLiteral("unsuppressed"));
    check(near(shown(), plate - hole * 20.0), QStringLiteral("unsuppressing restores it: %1 mm3").arg(vol(shown())));
    check(w.recompute()->computedFeatures() == computed, QStringLiteral("without recomputing anything"));

    // 7. Edit Feature (double-click in the timeline) reopens the same dialog
    // with the feature's values, rolled back to it. Extrude2 is looked at and
    // cancelled; Extrude1 stays open.
    for(int i : {3, 1}) {
        const auto f = std::dynamic_pointer_cast<const cad::ExtrudeFeature>(doc.features()[size_t(i)]);
        doubleClickAt(tl, tl->itemRect(i).center());
        ex = extrudeCommand();
        check(ex && ex->isEditing() && ex->editing() == f->id,
              QStringLiteral("double-clicking %1 opens Edit Feature").arg(QString::fromStdString(f->name)));
        if(!ex) return false;
        settle(QStringLiteral("edit %1").arg(QString::fromStdString(f->name)));
        const bool same = ex->profileCount() == int(f->profiles.size()) &&
                          ex->distanceField()->expression() == QString::fromStdString(f->distance.expr) &&
                          ex->taperField()->expression() == QString::fromStdString(f->taper.expr) &&
                          ex->extentBox()->currentIndex() == (f->extent == cad::ExtentType::ThroughAll ? 2 : 0) &&
                          ex->directionBox()->currentIndex() == 0 && ex->flipBox()->isChecked() == f->flip &&
                          ex->operation() == f->operation;
        check(same, QStringLiteral("  with the same values (%1, %2)")
                        .arg(ex->distanceField()->expression(), ex->extentBox()->currentText()));
        check(tl->markerX() == tl->itemRect(i).right() + 4, QStringLiteral("  and the timeline rolled back to it"));
        if(i == 3) w.commandPanel()->cancelButton()->click();
    }
    // Make Extrude1 30 mm: the preview updates before OK.
    ex = extrudeCommand();
    if(!ex) return false;
    ex->distanceField()->setFocus();
    ex->distanceField()->selectAll();
    typeText(w, QStringLiteral("30"));
    settle(QStringLiteral("edit preview"));
    check(w.modelView()->evaluation()->preview && near(shown(), 60.0 * 40.0 * 30.0),
          QStringLiteral("the edit previews live: %1 mm3").arg(vol(shown())));
    shot("plate_6_edit_feature.png");
    w.commandPanel()->okButton()->click();
    settle(QStringLiteral("commit the edit"));
    check(near(shown(), 60.0 * 40.0 * 30.0 - hole * 30.0),
          QStringLiteral("after OK the hole still goes through: %1 mm3").arg(vol(shown())));
    check(QString::fromStdString(doc.undoLabel()) == QStringLiteral("Edit Extrude1"), QStringLiteral("as one undo step"));

    // 8. Make the model take a while to recompute (their dialogs come in
    // later milestones, so these features are added directly): a grid of 16
    // holes cut through the plate, and every edge of the top and bottom faces
    // rounded. Then edit the first dimension: 60 -> 80 in Sketch1.
    {
        const cad::StatePtr st = w.modelView()->state();
        const cad::Body *body = st->orderedBodies().front();
        cad::TopoRef top;
        for(int i = 1; i <= body->shape.faceCount(); ++i) {
            gp_Pln pln;
            if(cad::planeOfFace(body->shape.face(i), pln) && pln.Axis().Direction().IsEqual(gp_Dir(0, 0, 1), 1e-9) &&
               pln.Location().Z() > 29.0)
                top = cad::makeTopoRef(*body, cad::TopoKind::Face, i);
        }
        auto grid = std::make_shared<cad::SketchFeature>();
        grid->plane = cad::PlaneRef::onFace(top);
        gp_Ax3 frame;
        cad::Status status;
        check(!top.empty() && cad::resolvePlane(*st, grid->plane, frame, status), QStringLiteral("the top face again"));
        auto local = [&](double x, double y) {
            const gp_Vec v(frame.Location(), gp_Pnt(x, y, 30.0));
            return cad::Vec2{v.Dot(gp_Vec(frame.XDirection())), v.Dot(gp_Vec(frame.YDirection()))};
        };
        for(double x : {6.0, 14.0, 22.0, 38.0, 46.0, 54.0})
            for(double y : {8.0, 20.0, 32.0})
                if(std::hypot(x - 30.0, y - 20.0) > 12.0) grid->sketch.addCircle(local(x, y), 2.0);
        const cad::FeatureId gridId = doc.addFeature(grid);
        auto holes = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &p : doc.stateAt(doc.marker())->sketches.at(gridId)->profiles)
            holes->profiles.push_back({gridId, p.key, p.sample});
        holes->extent = cad::ExtentType::ThroughAll;
        holes->flip = true;
        holes->operation = cad::BodyOperation::Cut;
        holes->distance = doc.makeSlot("10 mm");
        holes->taper = doc.makeSlot("0 deg");
        doc.addFeature(holes);
        settle(QStringLiteral("16 holes"));
        check(near(shown(), 60.0 * 40.0 * 30.0 - hole * 30.0 - 16 * cad::kPi * 4.0 * 30.0),
              QStringLiteral("%1 holes through the plate: %2 mm3").arg(holes->profiles.size()).arg(vol(shown())));

        const cad::Body *drilled = w.modelView()->state()->orderedBodies().front();
        auto fillet = std::make_shared<cad::FilletFeature>();
        for(int i = 1; i <= drilled->shape.faceCount(); ++i) {
            gp_Pln pln;
            if(cad::planeOfFace(drilled->shape.face(i), pln) && pln.Axis().Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9))
                fillet->faces.push_back(cad::makeTopoRef(*drilled, cad::TopoKind::Face, i));
        }
        fillet->radius = doc.makeSlot("1 mm");
        doc.addFeature(fillet);
        settle(QStringLiteral("fillet"));
        const cad::Status fs = doc.statusOf(fillet->id);
        check(doc.features().size() == 7 && fs.severity == cad::Severity::Ok,
              QStringLiteral("a fillet on the edges of %1 faces: %2 mm3 %3")
                  .arg(fillet->faces.size())
                  .arg(vol(shown()), QString::fromStdString(fs.message)));
    }
    doubleClickAt(tl, tl->itemRect(0).center());
    check(mode->active(), QStringLiteral("double-clicking Sketch1 edits it"));
    if(!mode->active()) return false;
    processEventsFor(450);
    ed = mode->editor();
    const cad::SkConstraint *width = nullptr;
    for(const auto &c : ed->sketch().constraints)
        if(cad::isDimension(c.type) && std::fabs(ed->dimensionValue(c) - 60.0) < 1e-9) width = &c;
    check(width != nullptr, QStringLiteral("the 60 mm dimension is there"));
    if(!width) return false;
    const int widthId = width->id;
    waitForFrames(vp, 1);
    const std::optional<QRectF> label = ed->dimensionRect(widthId);
    if(label) doubleClickAt(vp, label->center());
    check(mode->dimensionEditor() != nullptr, QStringLiteral("double-clicking it opens its value box"));
    if(auto *box = mode->dimensionEditor()) {
        box->selectAll();
        typeText(w, QStringLiteral("80"));
        sendKey(w, Qt::Key_Return);
    }
    processEventsFor(30);
    check(std::fabs(std::fabs(ed->profiles().front().area) - 3200.0) < 1e-6, QStringLiteral("the rectangle is 80 wide"));
    const size_t before = w.recompute()->computedFeatures();
    w.action(QStringLiteral("finishSketch"))->trigger();
    settle(QStringLiteral("recompute after the dimension edit"));
    check(w.recompute()->computedFeatures() - before == 7, QStringLiteral("all seven features were recomputed"));
    // The same design evaluated from scratch, synchronously.
    cad::Document fresh;
    std::string error;
    fresh.fromJson(doc.toJson(), error);
    const double expected = modelVolume(fresh.displayedState());
    const double sharp = 80.0 * 40.0 * 30.0 - hole * 30.0 - 16 * cad::kPi * 4.0 * 30.0;
    check(near(shown(), expected) && expected < sharp && expected > sharp - 600.0,
          QStringLiteral("the whole model follows: %1 mm3 (from scratch %2)").arg(vol(shown()), vol(expected)));
    bool allOk = true;
    for(const auto &f : fresh.features()) allOk &= fresh.statusOf(f->id).severity == cad::Severity::Ok;
    check(allOk, QStringLiteral("every feature survives the edit"));
    vp->setStandardView(StandardView::Home, false);
    vp->fitAll(false);
    shot("plate_7_final.png");

    // The marking menu on the canvas.
    const QPointF middle(vp->width() * 0.5, vp->height() * 0.5);
    sendMouse(vp, QEvent::MouseButtonPress, middle, Qt::RightButton, Qt::RightButton);
    sendMouse(vp, QEvent::MouseButtonRelease, middle, Qt::RightButton, Qt::NoButton);
    check(w.markingMenu()->isOpen(), QStringLiteral("right-click opens the marking menu"));
    shot("plate_8_marking_menu.png");
    w.markingMenu()->close();

    // 9. Undo everything.
    int steps = 0;
    while(doc.canUndo() && steps < 100) {
        w.action(QStringLiteral("undo"))->trigger();
        ++steps;
    }
    settle(QStringLiteral("undo all"));
    check(doc.features().empty() && shown() == 0.0 && w.modelView()->state()->bodies.empty(),
          QStringLiteral("%1 undo steps return to an empty design").arg(steps));
    shot("plate_9_undone.png");

    log << QStringLiteral("  slowest model %1 ms, slowest frame %2 ms\n")
               .arg(slowestModel, 0, 'f', 1)
               .arg(slowestFrame, 0, 'f', 1);
    check(worstStall < 50.0,
          QStringLiteral("the UI never stalled for 50 ms while the model recomputed (longest %1 ms)").arg(worstStall, 0, 'f', 1));
    return ok;
}

// M5: Offset Plane, Fillet, Chamfer, Hole and Combine through their dialogs,
// with screenshots of each live preview, then Edit Feature, the timeline and
// a check against a from-scratch evaluation.
bool featuresScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    Viewport *vp = w.viewport();
    cad::Document &doc = w.document();
    bool ok = true;
    auto check = [&](bool cond, const QString &what) {
        log << (cond ? "  ok   " : "  FAIL ") << what << "\n";
        log.flush();
        ok &= cond;
    };
    auto shot = [&](const char *name) {
        waitForFrames(vp, 2);
        processEventsFor(30);
        w.grab().save(out.filePath(QString::fromLatin1(name)));
    };
    auto settle = [&] {
        w.waitForModel(60000);
        waitForFrames(vp, 1);
    };
    auto at = [&](double x, double y, double z) { return vp->camera().project(QVector3D(float(x), float(y), float(z))); };
    auto type = [&](ValueField *f, const QString &text) {
        f->setFocus();
        f->selectAll();
        typeText(w, text);
    };
    auto shown = [&] { return modelVolume(w.modelView()->state()); };
    auto lastOk = [&] { return doc.statusOf(doc.features().back()->id).isOk(); };

    // A 60 x 40 x 20 plate (sketching and extruding are covered by `plate`).
    auto s = std::make_shared<cad::SketchFeature>();
    s->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
    s->sketch.addRectangle({0, 0}, {60, 40});
    const cad::FeatureId sid = doc.addFeature(s);
    auto e = std::make_shared<cad::ExtrudeFeature>();
    for(const auto &p : doc.stateAt(doc.marker())->sketches.at(sid)->profiles) e->profiles.push_back({sid, p.key, p.sample});
    e->distance = doc.makeSlot("20 mm");
    doc.addFeature(e);
    w.refresh();
    vp->setStandardView(StandardView::Home, false);
    vp->fitAll(false);
    waitForFrames(vp, 2);

    // Fillet the four vertical edges (clicks add them).
    w.action(QStringLiteral("fillet"))->trigger();
    auto *fillet = qobject_cast<FilletCommand *>(w.commands()->command());
    if(!fillet) return false;
    for(const auto &p : {std::array<double, 2>{0, 0}, {60, 0}, {60, 40}}) clickAt(vp, at(p[0], p[1], 10));
    type(fillet->radiusField(), QStringLiteral("6"));
    settle();
    check(fillet->edgeCount() == 3 && lastOk() && shown() < 48000.0 - 3 * 20 * 36 * (1 - cad::kPi / 4) + 1.0,
          QStringLiteral("Fillet: 3 edges at 6 mm previewed (%1 mm3; %2 edges, radius '%3', '%4')")
              .arg(shown(), 0, 'f', 2)
              .arg(fillet->edgeCount())
              .arg(fillet->radiusField()->expression(), w.commandPanel()->message()));
    shot("features_1_fillet.png");
    w.commandPanel()->okButton()->click();
    settle();

    // Chamfer the top face's edges.
    w.action(QStringLiteral("chamfer"))->trigger();
    auto *chamfer = qobject_cast<ChamferCommand *>(w.commands()->command());
    if(!chamfer) return false;
    clickAt(vp, at(30, 20, 20));
    type(chamfer->distanceField(), QStringLiteral("1.5"));
    settle();
    check(chamfer->edgeCount() == 1 && w.commandPanel()->okButton()->isEnabled(),
          QStringLiteral("Chamfer: the top face's edges at 1.5 mm previewed (%1 picked, '%2')")
              .arg(chamfer->edgeCount())
              .arg(w.commandPanel()->message()));
    shot("features_2_chamfer.png");
    w.commandPanel()->okButton()->click();
    settle();
    check(lastOk(), QStringLiteral("Chamfer committed"));

    // Two counterbored holes clicked onto the top face.
    w.action(QStringLiteral("hole"))->trigger();
    auto *hole = qobject_cast<HoleCommand *>(w.commands()->command());
    if(!hole) return false;
    hole->typeBox()->setCurrentIndex(1);
    hole->extentBox()->setCurrentIndex(1);
    clickAt(vp, at(18, 20, 20));
    clickAt(vp, at(42, 20, 20));
    settle();
    check(hole->holeCount() == 2 && w.modelView()->evaluation()->tool != nullptr,
          QStringLiteral("Hole: two counterbored holes, the drills shown translucent"));
    shot("features_3_hole.png");
    w.commandPanel()->okButton()->click();
    settle();
    check(lastOk(), QStringLiteral("Hole committed"));

    // An offset plane above the plate, tilted 20 degrees about X and 15 about Y.
    w.action(QStringLiteral("offsetPlane"))->trigger();
    auto *plane = qobject_cast<PlaneCommand *>(w.commands()->command());
    if(!plane) return false;
    clickAt(vp, at(30, 8, 20));
    type(plane->offsetField(), QStringLiteral("25"));
    type(plane->angleField(), QStringLiteral("20"));
    type(plane->tiltYField(), QStringLiteral("15"));
    settle();
    check(plane->hasBase() && w.modelView()->state()->planes.size() == 1, QStringLiteral("Offset Plane previewed"));
    shot("features_4_plane.png");
    {
        // Hover the Tilt Y knob: its ring lights up with degree marks and the
        // canvas value box moves to it. Then close-ups of the rings and dials.
        const QPointF knob = plane->gizmo().knobOnScreen(1);
        sendMouse(vp, QEvent::MouseMove, knob, Qt::NoButton, Qt::NoButton);
        check(plane->gizmo().hot() == PlaneGizmo::Part::Ring1 && w.commands()->command()->canvasValue() == plane->tiltYField(),
              QStringLiteral("hovering a ring's knob puts the canvas value on that tilt"));
        shot("features_4b_plane_ring.png");
        const QPoint c = vp->mapTo(&w, vp->camera().project(plane->gizmo().ring(0).center).toPoint());
        const QImage full = w.grab().toImage();
        const qreal dpr = full.devicePixelRatio();
        const QRect crop(QPoint(int((c.x() - 150) * dpr), int((c.y() - 130) * dpr)), QSize(int(300 * dpr), int(260 * dpr)));
        full.copy(crop).scaled(QSize(900, 780), Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(out.filePath(QStringLiteral("features_4c_tilt_closeup.png")));
        // The two dials, drawn at 4x.
        QImage dials(QSize(2 * 44 + 12, 44) * 4, QImage::Format_ARGB32_Premultiplied);
        dials.setDevicePixelRatio(4);
        dials.fill(Qt::white);
        QPainter dp(&dials);
        int x = 0;
        for(ValueField *f : {plane->angleField(), plane->tiltYField()}) {
            w.commandPanel()->angleDial(f)->render(&dp, QPoint(x, 0));
            x += 56;
        }
        dp.end();
        dials.save(out.filePath(QStringLiteral("features_4d_dials.png")));
        sendMouse(vp, QEvent::MouseMove, QPointF(5, 5), Qt::NoButton, Qt::NoButton);
    }
    w.commandPanel()->okButton()->click();
    settle();
    check(lastOk(), QStringLiteral("Offset Plane committed"));

    // A second body, then Combine / Cut it from the plate.
    auto s2 = std::make_shared<cad::SketchFeature>();
    s2->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
    s2->sketch.addCircle(cad::Vec2{60, 40}, 12);
    const cad::FeatureId sid2 = doc.addFeature(s2);
    auto e2 = std::make_shared<cad::ExtrudeFeature>();
    for(const auto &p : doc.stateAt(doc.marker())->sketches.at(sid2)->profiles) e2->profiles.push_back({sid2, p.key, p.sample});
    e2->distance = doc.makeSlot("30 mm");
    e2->operation = cad::BodyOperation::NewBody;
    doc.addFeature(e2);
    w.refresh();
    waitForFrames(vp, 2);
    const double before = shown();
    w.action(QStringLiteral("combine"))->trigger();
    auto *combine = qobject_cast<CombineCommand *>(w.commands()->command());
    if(!combine) return false;
    clickAt(vp, at(10, 20, 20));
    clickAt(vp, at(60 + 12 * 0.7, 40 - 12 * 0.7, 30));
    combine->operationBox()->setCurrentIndex(1);
    settle();
    check(combine->hasTarget() && combine->toolCount() == 1 && w.modelView()->state()->bodies.size() == 1 &&
              shown() < before - 1000.0,
          QStringLiteral("Combine: cutting the cylinder out of the plate previewed"));
    shot("features_5_combine.png");
    w.commandPanel()->okButton()->click();
    settle();
    check(lastOk(), QStringLiteral("Combine committed"));

    // Edit Feature on the fillet: the same dialog with its three edges.
    w.editFeature(doc.features()[2]->id);
    fillet = qobject_cast<FilletCommand *>(w.commands()->command());
    check(fillet && fillet->isEditing() && fillet->edgeCount() == 3 &&
              fillet->radiusField()->expression() == QStringLiteral("6"),
          QStringLiteral("Edit Feature reopens the fillet with its edges and radius"));
    shot("features_6_edit_fillet.png");
    w.commandPanel()->cancelButton()->click();
    settle();

    // Everything recomputes the same from scratch.
    cad::Document fresh;
    std::string error;
    fresh.fromJson(doc.toJson(), error);
    bool allOk = true;
    for(const auto &f : fresh.features()) allOk &= fresh.statusOf(f->id).isOk();
    check(allOk && std::fabs(modelVolume(fresh.displayedState()) - shown()) < 1e-6,
          QStringLiteral("the design recomputes from scratch to the same %1 mm3").arg(shown(), 0, 'f', 2));
    vp->setStandardView(StandardView::Home, false);
    vp->fitAll(false);
    shot("features_7_done.png");

    // Measure between the two counterbores' rims: 24 mm centre to centre.
    doc.setFolderVisible("construction", false);
    w.refresh();
    vp->fitAll(false);
    waitForFrames(vp, 2);
    w.action(QStringLiteral("measure"))->trigger();
    auto *measure = qobject_cast<MeasureCommand *>(w.commands()->command());
    if(!measure) return false;
    // Off the rims' seam vertices (at +X).
    clickAt(vp, at(18 - 4.5 * 0.5, 20 - 4.5 * 0.866, 20));
    clickAt(vp, at(42 + 4.5 * 0.5, 20 - 4.5 * 0.866, 20));
    waitForFrames(vp, 1);
    check(measure->result() && measure->result()->centreDistance &&
              std::fabs(*measure->result()->centreDistance - 24.0) < 1e-6,
          QStringLiteral("Measure: the counterbores are 24 mm apart centre to centre (%1)")
              .arg(measure->resultText().replace(QLatin1Char('\n'), QStringLiteral("; "))));
    shot("features_8_measure.png");
    w.commandPanel()->cancelButton()->click();

    // Overhang analysis, with a cylinder lying beside the plate: its underside
    // goes from fine (green) through near the limit (amber) and needing
    // support (red) to a flat bridge (blue).
    auto cyl = std::make_shared<cad::SketchFeature>();
    cyl->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XZ);
    cyl->sketch.addCircle(cad::Vec2{95, 18}, 12);
    const cad::FeatureId cid = doc.addFeature(cyl);
    auto ce = std::make_shared<cad::ExtrudeFeature>();
    for(const auto &p : doc.stateAt(doc.marker())->sketches.at(cid)->profiles) ce->profiles.push_back({cid, p.key, p.sample});
    ce->distance = doc.makeSlot("-40 mm");
    ce->operation = cad::BodyOperation::NewBody;
    doc.addFeature(ce);
    w.refresh();
    settle();
    // Seen from below the plate, in front and to the left, to show the undersides.
    vp->camera().setOrientation(QVector3D(0.55f, 0.75f, 0.38f).normalized(), QVector3D(0, 0, 1));
    vp->fitAll(false);
    w.action(QStringLiteral("overhangs"))->trigger();
    auto *over = qobject_cast<OverhangCommand *>(w.commands()->command());
    if(!over) return false;
    settle();
    const auto &areas = w.modelView()->overhangAreas();
    check(areas[size_t(cad::OverhangKind::Overhang)] > 100.0 && areas[size_t(cad::OverhangKind::Near)] > 10.0,
          QStringLiteral("Overhangs: the lying cylinder's underside needs support (%1 mm2, %2 mm2 near the limit)")
              .arg(areas[size_t(cad::OverhangKind::Overhang)], 0, 'f', 1)
              .arg(areas[size_t(cad::OverhangKind::Near)], 0, 'f', 1));
    shot("features_9_overhangs.png");
    w.commandPanel()->okButton()->click();

    // Split the plate in two at x = 30 with alignment pins, as for a part
    // printed in pieces.
    auto cut = std::make_shared<cad::ConstructionPlaneFeature>();
    cut->base = cad::PlaneRef::origin(cad::PlaneRef::Kind::YZ);
    cut->offset = doc.makeSlot("30 mm");
    const cad::FeatureId cutId = doc.addFeature(cut);
    doc.setFolderVisible("construction", true);
    w.refresh();
    settle();
    vp->setStandardView(StandardView::Home, false);
    vp->fitAll(false);
    waitForFrames(vp, 1);
    const size_t before9 = w.modelView()->state()->bodies.size();
    w.action(QStringLiteral("split"))->trigger();
    auto *split = qobject_cast<SplitCommand *>(w.commands()->command());
    if(!split) return false;
    for(const auto &[item, q] : w.modelView()->planeQuads())
        if(item.kind == SelectionItem::Kind::Plane && item.feature == cutId)
            for(int k = 0; k < 4 && !split->hasPlane(); ++k)
                clickAt(vp, vp->camera().project(q[size_t(k)] * 0.85f + q[size_t((k + 2) % 4)] * 0.15f));
    split->pinsBox()->setChecked(true);
    settle();
    check(split->hasPlane() && w.modelView()->state()->bodies.size() == before9 + 1,
          QStringLiteral("Split: the plate cut in two at x = 30 with alignment pins (%1 bodies, '%2')")
              .arg(w.modelView()->state()->bodies.size())
              .arg(w.commandPanel()->message()));
    shot("features_10_split.png");
    w.commandPanel()->okButton()->click();
    settle();
    check(lastOk(), QStringLiteral("Split committed"));
    doc.setFolderVisible("construction", false);
    w.refresh();
    settle();
    shot("features_11_split_done.png");
    return ok;
}

// M6 acceptance: Section Analysis through its dialog on the demo bracket (a
// hatched cut through both holes), the depth arrow dragged afterwards, a
// fillet on an edge picked in the section view, the browser eye turning it
// off and on, and an upstream edit moving the cut with the model.
bool sectionScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    Viewport *vp = w.viewport();
    cad::Document &doc = w.document();
    bool ok = true;
    auto check = [&](bool cond, const QString &what) {
        log << (cond ? "  ok   " : "  FAIL ") << what << "\n";
        log.flush();
        ok &= cond;
    };
    auto shot = [&](const char *name) {
        waitForFrames(vp, 2);
        processEventsFor(30);
        w.grab().save(out.filePath(QString::fromLatin1(name)));
    };
    auto settle = [&] {
        w.waitForModel(60000);
        waitForFrames(vp, 1);
    };
    auto at = [&](double x, double y, double z) { return vp->camera().project(QVector3D(float(x), float(y), float(z))); };
    auto type = [&](ValueField *f, const QString &text) {
        f->setFocus();
        f->selectAll();
        typeText(w, text);
    };
    // Is a point cut away by the section in use?
    auto cut = [&](double x, double y, double z) {
        const auto c = w.modelView()->clipPlane();
        return c && QVector3D::dotProduct(c->toVector3D(), QVector3D(float(x), float(y), float(z))) + c->w() > 0;
    };
    // Hatching: light cap and dark lines next to each other around a point.
    auto hatched = [&](QPointF p) {
        waitForFrames(vp, 2);
        const QImage img = vp->grabFramebuffer();
        const QPoint c(int(p.x() * img.width() / vp->width()), int(p.y() * img.height() / vp->height()));
        int lo = 255, hi = 0;
        for(int dx = -8; dx <= 8; ++dx) {
            const int v = qGray(img.pixel(std::clamp(c.x() + dx, 0, img.width() - 1), c.y()));
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        return hi - lo > 40;
    };

    buildDemoBracket(doc);
    w.refresh();
    vp->setStandardView(StandardView::Home, false);
    vp->fitAll(false);
    waitForFrames(vp, 2);

    // 1. Inspect > Section Analysis on the XZ plane, 20 mm in: through both holes.
    w.action(QStringLiteral("sectionAnalysis"))->trigger();
    auto *cmd = qobject_cast<SectionCommand *>(w.commands()->command());
    if(!cmd) return false;
    QPointF xz;
    for(const auto &[item, q] : w.modelView()->planeQuads())
        if(item.key == "XZ") xz = vp->camera().project(q[0] + (q[1] - q[0]) * 0.92f + (q[3] - q[0]) * 0.92f);
    clickAt(vp, xz);
    check(cmd->hasPlane(), QStringLiteral("the XZ origin plane is picked"));
    type(cmd->distanceField(), QStringLiteral("-20"));
    settle();
    check(cut(30, 10, 6) && !cut(30, 30, 6) && doc.sections().empty(),
          QStringLiteral("the cut is previewed (front half away) before OK"));
    shot("section_1_command.png");
    w.commandPanel()->okButton()->click();
    settle();
    check(doc.activeSection() && doc.sections().size() == 1, QStringLiteral("OK keeps Section1 (Analysis folder)"));
    check(hatched(at(30, 20, 6)), QStringLiteral("the cut face is hatched"));
    shot("section_2_hatched.png");

    // 2. The depth arrow stays on the canvas: drag it 8 mm back (to y = 12).
    DistanceManipulator *arrow = w.sectionArrow();
    check(arrow->visible(), QStringLiteral("the depth arrow is on the canvas"));
    const QVector3D target = arrow->origin() + arrow->direction() * float(arrow->distance() + 8.0);
    dragAt(vp, arrow->headOnScreen(), at(target.x(), target.y(), target.z()));
    settle();
    check(doc.activeSection() && std::fabs(doc.activeSection()->offset + 12.0) < 0.05 && cut(30, 10, 6) && !cut(30, 14, 6),
          QStringLiteral("dragging the arrow moves the cut (depth %1)").arg(doc.activeSection()->offset));
    shot("section_3_dragged.png");

    // 3. Modelling goes on: fillet the back top edge, picked in the section view.
    w.action(QStringLiteral("fillet"))->trigger();
    auto *fillet = qobject_cast<FilletCommand *>(w.commands()->command());
    if(!fillet) return false;
    clickAt(vp, at(30, 0, 12)); // the front top edge is cut away: nothing there
    clickAt(vp, at(30, 40, 12));
    type(fillet->radiusField(), QStringLiteral("3"));
    settle();
    check(fillet->edgeCount() == 1, QStringLiteral("only the edge that is still there is picked"));
    shot("section_4_fillet.png");
    w.commandPanel()->okButton()->click();
    settle();
    check(doc.statusOf(doc.features().back()->id).isOk() && w.modelView()->clipPlane().has_value(),
          QStringLiteral("the fillet is made while the section stays on"));

    // 4. The eye in the browser: back to the normal view, and on again.
    doc.setSectionVisible(doc.sections().front().id, false);
    settle();
    check(!w.modelView()->clipPlane() && !arrow->visible(), QStringLiteral("hidden: the normal view is back"));
    shot("section_5_off.png");
    doc.setSectionVisible(doc.sections().front().id, true);
    settle();
    check(w.modelView()->clipPlane().has_value(), QStringLiteral("shown again"));

    // 5. Looking straight at the cut.
    vp->setStandardView(StandardView::Front, false);
    vp->fitAll(false);
    check(hatched(at(30, 12, 6)), QStringLiteral("the cap seen head-on is hatched"));
    shot("section_6_front.png");
    return ok;
}

// Fraction of the frame that is not background, and how different two frames are.
double partFraction(const Viewport *vp, const QImage &img) {
    size_t part = 0;
    for(int y = 0; y < img.height(); y += 2) {
        const QColor bg = expectedBackground(vp, img, y);
        for(int x = 0; x < img.width(); x += 2) {
            const QRgb c = img.pixel(x, y);
            part += std::abs(qRed(c) - bg.red()) + std::abs(qGreen(c) - bg.green()) + std::abs(qBlue(c) - bg.blue()) > 36;
        }
    }
    return double(part) / double(((img.width() + 1) / 2) * ((img.height() + 1) / 2));
}

double meanDifference(const QImage &a, const QImage &b) {
    if(a.size() != b.size()) return 255.0;
    double sum = 0;
    size_t n = 0;
    for(int y = 0; y < a.height(); y += 2)
        for(int x = 0; x < a.width(); x += 2, ++n) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            sum += (std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q))) / 3.0;
        }
    return n ? sum / double(n) : 0.0;
}

// The dialog that `action` opens (the visible one: a closed one may not be deleted yet).
ExportDialog *openedExportDialog(MainWindow &w, const char *action) {
    w.action(QString::fromLatin1(action))->trigger();
    for(ExportDialog *d : w.findChildren<ExportDialog *>())
        if(d->isVisible()) return d;
    return nullptr;
}

// Presses Export and waits for it to finish. `background`: the click returned
// with the export still running (on its worker thread); `stall`: the longest
// the UI thread was kept from running meanwhile.
bool pressExport(ExportDialog *dlg, const QString &file, bool &background, double &stall) {
    dlg->setOutputPath(file);
    dlg->setAskBeforeWritingInvalid(false);
    auto *button = dlg->findChild<QPushButton *>(QStringLiteral("exportButton"));
    if(!button || !button->isEnabled()) return false;
    QEventLoop loop;
    bool finished = false;
    QObject::connect(dlg, &ExportDialog::exportFinished, &loop, [&] {
        finished = true;
        loop.quit();
    });
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    StallMeter meter;
    meter.start();
    button->click();
    background = dlg->busy() && !finished;
    if(!finished) loop.exec();
    stall = meter.stop();
    return finished;
}

// M7 acceptance, all through the UI: a printable L bracket (sketches with
// typed dimensions, extrudes, counterbored and through holes, a fillet and
// chamfers), a parametric edit of the first extrude that everything follows,
// screenshots in every visual style, 3D Print to an STL that passes the
// printability check (and reads back as a valid mesh), File > Export to a STEP
// that reads back with the same volume, and a save / reopen round trip.
bool acceptanceScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    Viewport *vp = w.viewport();
    SketchMode *mode = w.sketchMode();
    cad::Document &doc = w.document();
    vp->setStandardView(StandardView::Home, false);
    if(!waitForFrames(vp, 2)) return false;
    bool ok = true;
    auto check = [&](bool cond, const QString &what) {
        log << (cond ? "  ok   " : "  FAIL ") << what << "\n";
        log.flush();
        ok &= cond;
    };
    auto shot = [&](const char *name) {
        waitForFrames(vp, 2);
        processEventsFor(30);
        w.grab().save(out.filePath(QString::fromLatin1(name)));
    };
    auto settle = [&] {
        w.waitForModel(60000);
        waitForFrames(vp, 1);
    };
    auto home = [&] {
        vp->setStandardView(StandardView::Home, false);
        vp->fitAll(false);
        waitForFrames(vp, 1);
    };
    auto at = [&](double x, double y, double z) { return vp->camera().project(QVector3D(float(x), float(y), float(z))); };
    auto type = [&](ValueField *f, const QString &text) {
        f->setFocus();
        f->selectAll();
        typeText(w, text);
    };
    auto shown = [&] { return modelVolume(w.modelView()->state()); };
    auto vol = [](double v) { return QString::number(v, 'f', 2); };
    auto allOk = [&](cad::Document &d) {
        for(const auto &f : d.features())
            if(!d.statusOf(f->id).isOk()) return false;
        return true;
    };
    auto lastOk = [&] { return !doc.features().empty() && doc.statusOf(doc.features().back()->id).isOk(); };
    auto extrudeCommand = [&] { return qobject_cast<ExtrudeCommand *>(w.commands()->command()); };
    // Draws a rectangle between two world points on the open sketch's plane,
    // typing its size in the heads-up boxes.
    auto rectangle = [&](QVector3D a, QVector3D b) {
        SketchEditor *ed = mode->editor();
        const auto p = ed->toSketch(vp->camera().project(a)), q = ed->toSketch(vp->camera().project(b));
        if(!p || !q) return false;
        w.action(QStringLiteral("sketchRectangle"))->trigger();
        clickAt(vp, ed->toScreen(*p));
        sendMouse(vp, QEvent::MouseMove, ed->toScreen({p->x + (q->x - p->x) * 0.7, p->y + (q->y - p->y) * 0.7}),
                  Qt::NoButton, Qt::NoButton);
        // Sizes come from projected float points; round off the float noise so
        // the rectangle lands exactly on the intended edges.
        typeText(w, QString::number(std::round(std::fabs(q->x - p->x) * 1000.0) / 1000.0));
        sendKey(w, Qt::Key_Tab);
        typeText(w, QString::number(std::round(std::fabs(q->y - p->y) * 1000.0) / 1000.0));
        sendKey(w, Qt::Key_Return);
        sendKey(w, Qt::Key_Escape);
        return true;
    };

    // 1. The base: a 60 x 40 rectangle on the XY plane, extruded 8 mm.
    w.action(QStringLiteral("createSketch"))->trigger();
    const float s = vp->camera().viewHeightAtTarget() * 0.2f;
    clickAt(vp, vp->camera().project(QVector3D(s * 0.85f, s * 0.85f, 0)));
    if(!mode->active()) {
        check(false, QStringLiteral("Create Sketch on the XY plane"));
        return false;
    }
    processEventsFor(450);
    rectangle({0, 0, 0}, {60, 40, 0});
    w.action(QStringLiteral("extrude"))->trigger();
    ExtrudeCommand *ex = extrudeCommand();
    if(!ex) return false;
    settle();
    typeText(w, QStringLiteral("8"));
    settle();
    sendKey(w, Qt::Key_Return);
    settle();
    const double base = 60.0 * 40.0 * 8.0;
    check(doc.features().size() == 2 && std::fabs(shown() - base) < 1e-6,
          QStringLiteral("base plate 60 x 40 x 8 (%1 mm3)").arg(vol(shown())));

    // 2. The upright: a 60 x 8 rectangle on the plate's top face, extruded 40 mm up (joins).
    home();
    w.action(QStringLiteral("createSketch"))->trigger();
    clickAt(vp, at(30, 20, 8));
    if(!mode->active()) {
        check(false, QStringLiteral("Create Sketch on the top face"));
        return false;
    }
    processEventsFor(450);
    rectangle({60, 40, 8}, {0, 32, 8}); // from the plate's back corner, which it snaps to
    w.action(QStringLiteral("extrude"))->trigger();
    ex = extrudeCommand();
    if(!ex) return false;
    settle();
    typeText(w, QStringLiteral("40"));
    check(ex->operation() == cad::BodyOperation::NewBody, QStringLiteral("an extrude makes a new body by default"));
    ex->operationBox()->setCurrentIndex(0); // Operation: Join
    emit ex->operationBox()->activated(0);
    settle();
    check(ex->operation() == cad::BodyOperation::Join, QStringLiteral("Join chosen in the dialog"));
    sendKey(w, Qt::Key_Return);
    settle();
    const double bracket = base + 60.0 * 8.0 * 40.0;
    check(doc.features().size() == 4 && w.modelView()->state()->bodies.size() == 1 &&
              std::fabs(shown() - bracket) < 0.01, // solver error and the booleans' 0.1 um fuzz
          QStringLiteral("upright joined: one body, %1 mm3").arg(vol(shown())));

    // 3. Two counterbored screw holes through the base.
    home();
    w.action(QStringLiteral("hole"))->trigger();
    auto *hole = qobject_cast<HoleCommand *>(w.commands()->command());
    if(!hole) return false;
    hole->typeBox()->setCurrentIndex(1); // counterbore
    hole->extentBox()->setCurrentIndex(1); // all
    clickAt(vp, at(14, 14, 8));
    clickAt(vp, at(46, 14, 8));
    settle();
    check(hole->holeCount() == 2 && w.modelView()->evaluation()->tool != nullptr,
          QStringLiteral("two counterbored holes previewed"));
    w.commandPanel()->okButton()->click();
    settle();
    const double screws = 2.0 * cad::kPi * (2.5 * 2.5 * 5.0 + 4.5 * 4.5 * 3.0);
    check(lastOk() && std::fabs(shown() - (bracket - screws)) < 0.01,
          QStringLiteral("counterbored holes: %1 mm3").arg(vol(shown())));

    // 4. A through hole in the upright, placed on its front face.
    w.action(QStringLiteral("hole"))->trigger();
    hole = qobject_cast<HoleCommand *>(w.commands()->command());
    if(!hole) return false;
    hole->typeBox()->setCurrentIndex(0);
    hole->extentBox()->setCurrentIndex(1);
    type(hole->diameterField(), QStringLiteral("8"));
    clickAt(vp, at(30, 32, 30));
    settle();
    w.commandPanel()->okButton()->click();
    settle();
    const double drilled = bracket - screws - cad::kPi * 16.0 * 8.0;
    check(lastOk() && std::fabs(shown() - drilled) < 0.01, QStringLiteral("hole through the upright: %1 mm3").arg(vol(shown())));

    // 5. Fillet the inside corner between base and upright.
    w.action(QStringLiteral("fillet"))->trigger();
    auto *fillet = qobject_cast<FilletCommand *>(w.commands()->command());
    if(!fillet) return false;
    clickAt(vp, at(12, 32, 8));
    type(fillet->radiusField(), QStringLiteral("4"));
    settle();
    check(fillet->edgeCount() == 1, QStringLiteral("the inside edge is picked"));
    w.commandPanel()->okButton()->click();
    settle();
    const double filleted = drilled + (16.0 - cad::kPi * 4.0) * 60.0;
    check(lastOk() && std::fabs(shown() - filleted) < 0.05, QStringLiteral("inside fillet R4: %1 mm3").arg(vol(shown())));

    // 6. Chamfer the upright's top face and the base's front corners.
    w.action(QStringLiteral("chamfer"))->trigger();
    auto *chamfer = qobject_cast<ChamferCommand *>(w.commands()->command());
    if(!chamfer) return false;
    clickAt(vp, at(30, 36, 48));
    clickAt(vp, at(0, 0, 4));
    clickAt(vp, at(60, 0, 4));
    type(chamfer->distanceField(), QStringLiteral("1"));
    settle();
    check(chamfer->edgeCount() == 3, QStringLiteral("a face and two edges picked for the chamfer"));
    w.commandPanel()->okButton()->click();
    settle();
    check(lastOk() && shown() < filleted - 1.0, QStringLiteral("chamfers: %1 mm3").arg(vol(shown())));
    home();
    shot("acceptance_1_bracket.png");

    // 7. Parametric edit: Extrude1 from 8 to 10 mm; everything downstream follows.
    const double before = shown();
    const cad::FeatureId first = doc.features()[1]->id;
    w.editFeature(first);
    ex = extrudeCommand();
    check(ex && ex->isEditing() && ex->distanceField()->expression() == QStringLiteral("8"),
          QStringLiteral("Edit Feature reopens Extrude1 at 8 mm"));
    if(!ex) return false;
    type(ex->distanceField(), QStringLiteral("10"));
    settle();
    w.commandPanel()->okButton()->click();
    settle();
    const double grown = shown() - before;
    check(allOk(doc) && doc.marker() == int(doc.features().size()) && grown > 4600.0 && grown < 4800.0,
          QStringLiteral("every feature follows the thicker base (+%1 mm3)").arg(vol(grown)));
    cad::Document fresh;
    std::string error;
    fresh.fromJson(doc.toJson(), error);
    check(allOk(fresh) && std::fabs(modelVolume(fresh.displayedState()) - shown()) < 1e-6,
          QStringLiteral("the design recomputes from scratch to the same %1 mm3").arg(vol(shown())));

    // 8. Every visual style, from the View menu.
    home();
    w.modelView()->clearSelection();
    const std::tuple<const char *, const char *, DisplayStyle> styles[] = {
        {"displayWireframe", "acceptance_2_wireframe.png", DisplayStyle::Wireframe},
        {"displayShaded", "acceptance_3_shaded.png", DisplayStyle::Shaded},
        {"displayShadedEdges", "acceptance_4_shaded_edges.png", DisplayStyle::ShadedWithEdges},
        {"displayRendered", "acceptance_5_rendered.png", DisplayStyle::Rendered}};
    std::map<DisplayStyle, QImage> frames;
    for(const auto &[action, file, style] : styles) {
        w.action(QString::fromLatin1(action))->trigger();
        waitForFrames(vp, 3);
        check(vp->displayStyle() == style, QStringLiteral("View > %1").arg(w.action(QString::fromLatin1(action))->text()));
        frames[style] = vp->grabFramebuffer();
        frames[style].save(out.filePath(QString::fromLatin1(file)));
    }
    const double wire = partFraction(vp, frames[DisplayStyle::Wireframe]),
                 shaded = partFraction(vp, frames[DisplayStyle::Shaded]),
                 rendered = partFraction(vp, frames[DisplayStyle::Rendered]);
    log << QStringLiteral("         part covers %1% shaded, %2% wireframe, %3% rendered\n")
               .arg(shaded * 100, 0, 'f', 1)
               .arg(wire * 100, 0, 'f', 1)
               .arg(rendered * 100, 0, 'f', 1);
    check(shaded > 0.04 && rendered > 0.04 && wire > 0.002 && wire < shaded * 0.6,
          QStringLiteral("shaded and rendered fill the part, wireframe draws only its edges"));
    const double styleDiff = meanDifference(frames[DisplayStyle::Rendered], frames[DisplayStyle::ShadedWithEdges]);
    check(styleDiff > 1.0, QStringLiteral("rendered differs from shaded (mean %1 levels)").arg(styleDiff, 0, 'f', 1));
    w.action(QStringLiteral("displayShadedEdges"))->trigger();
    waitForFrames(vp, 1);

    // 9. MAKE > 3D Print: a watertight STL, checked before it is written.
    const double design = shown();
    ExportDialog *dlg = openedExportDialog(w, "print3d");
    check(dlg && dlg->format() == ExportJob::Format::Stl, QStringLiteral("3D Print opens the export dialog on STL"));
    if(!dlg) return false;
    dlg->refinementBox()->setCurrentIndex(2); // fine
    const QString stlPath = out.filePath(QStringLiteral("acceptance.stl"));
    QFile::remove(stlPath);
    bool background = false;
    double stall = 0;
    check(pressExport(dlg, stlPath, background, stall), QStringLiteral("the STL export finishes"));
    const ExportResult stl = dlg->lastResult();
    log << "         " << dlg->report().replace(QStringLiteral("<br>"), QStringLiteral(" | ")) << "\n";
    check(stl.ok && stl.meshChecked && stl.report.ok && stl.report.shells == 1,
          QStringLiteral("STL written: %1 triangles, one watertight shell").arg(stl.report.triangles));
    check(std::fabs(stl.solidVolume - design) < 1e-6 * design,
          QStringLiteral("what was exported is the design (%1 mm3)").arg(vol(stl.solidVolume)));
    check(background, QStringLiteral("it ran in the background (the UI's longest stall meanwhile %1 ms)").arg(stall, 0, 'f', 1));
    cad::TriMesh mesh;
    std::string readError;
    const bool read = cad::readStlFile(stlPath.toStdString(), mesh, readError);
    const cad::MeshReport back = read ? cad::validateMesh(mesh, design) : cad::MeshReport{};
    check(read && back.ok && back.boundaryEdges == 0 && back.nonManifoldEdges == 0 && back.flippedEdges == 0,
          QStringLiteral("the STL file reads back manifold: %1").arg(QString::fromStdString(read ? back.summary() : readError)));
    dlg->grab().save(out.filePath(QStringLiteral("acceptance_6_print_dialog.png")));
    dlg->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // 10. File > Export: STEP, read back with the same volume.
    dlg = openedExportDialog(w, "export");
    check(dlg && dlg->format() == ExportJob::Format::Step, QStringLiteral("File > Export opens on STEP"));
    if(!dlg) return false;
    const QString stepPath = out.filePath(QStringLiteral("acceptance.step"));
    QFile::remove(stepPath);
    check(pressExport(dlg, stepPath, background, stall) && background, QStringLiteral("the STEP export finishes (in the background)"));
    const ExportResult step = dlg->lastResult();
    check(step.ok && step.reimported && std::fabs(step.reimportedVolume - design) < 1e-6 * design,
          QStringLiteral("STEP written and read back: %1 mm3 (design %2 mm3)").arg(vol(step.reimportedVolume), vol(design)));
    std::vector<cad::NamedSolid> solids;
    check(cad::readStepFile(stepPath.toStdString(), solids, readError) && solids.size() == 1 &&
              QFileInfo(stepPath).size() > 1000,
          QStringLiteral("the STEP file holds the one bracket"));
    dlg->grab().save(out.filePath(QStringLiteral("acceptance_7_export_dialog.png")));
    dlg->reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // 11. Save, start over, and open it again.
    const QString designPath = out.filePath(QStringLiteral("acceptance.cadly"));
    check(w.saveFile(designPath), QStringLiteral("saved %1").arg(QFileInfo(designPath).fileName()));
    const size_t features = doc.features().size();
    w.newDocument();
    check(doc.features().empty(), QStringLiteral("New Design is empty"));
    check(w.openFile(designPath), QStringLiteral("reopened"));
    settle();
    check(doc.features().size() == features && allOk(doc) && std::fabs(shown() - design) < 1e-6 * design,
          QStringLiteral("the reopened design is the same (%1 features, %2 mm3)").arg(doc.features().size()).arg(vol(shown())));
    home();
    shot("acceptance_8_reopened.png");
    return ok;
}

// MCP: an agent (this scenario, over HTTP) connects, builds a plate with
// fillets and holes through tools, exports a checked STL; the toolbar's MCP
// button glows while it is connected; screenshots of the button, the event
// log and the settings.
bool mcpScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    using json = nlohmann::json;
    Viewport *vp = w.viewport();
    bool ok = true;
    auto check = [&](bool cond, const QString &what) {
        log << (cond ? "  ok   " : "  FAIL ") << what << "\n";
        log.flush();
        ok &= cond;
    };
    quint16 port = 0;
    {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        port = probe.serverPort();
    }
    McpSettings s;
    s.enabled = true;
    s.port = port;
    s.token = McpSettings::generateToken();
    check(w.mcpServer()->apply(s), QStringLiteral("the server starts on port %1").arg(port));
    check(w.mcpButton()->state() == McpServer::State::Listening, QStringLiteral("the MCP button shows it listening"));
    QNetworkAccessManager net;
    net.setProxy(QNetworkProxy::NoProxy);
    QByteArray session;
    auto rpc = [&](const std::string &method, const json &params) {
        QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Authorization", "Bearer " + s.token.toLatin1());
        if(!session.isEmpty()) req.setRawHeader("Mcp-Session-Id", session);
        QNetworkReply *r = net.post(req, QByteArray::fromStdString(json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}}.dump()));
        QEventLoop loop;
        QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(120000, &loop, &QEventLoop::quit);
        if(!r->isFinished()) loop.exec();
        if(!r->rawHeader("Mcp-Session-Id").isEmpty()) session = r->rawHeader("Mcp-Session-Id");
        const QByteArray body = r->readAll();
        r->deleteLater();
        return json::parse(body.constData(), body.constData() + body.size(), nullptr, false);
    };
    auto tool = [&](const std::string &name, const json &args) -> json {
        const json r = rpc("tools/call", {{"name", name}, {"arguments", args}});
        if(!r.contains("result")) return json();
        const std::string text = r["result"]["content"][0].value("text", "");
        const json j = json::parse(text, nullptr, false);
        if(r["result"].value("isError", false)) log << "         " << QString::fromStdString(name) << ": " << QString::fromStdString(text) << "\n";
        return j.is_discarded() ? json(text) : j;
    };
    const json init = rpc("initialize", {{"protocolVersion", "2025-06-18"}, {"clientInfo", {{"name", "Cadly selftest agent"}, {"version", "1"}}}});
    check(init.contains("result"), QStringLiteral("an agent connects (initialize)"));
    check(w.mcpButton()->state() == McpServer::State::Connected, QStringLiteral("the MCP button turns green"));
    json rect = {{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {70, 40}}};
    const json sketch = tool("create_sketch", {{"plane", "XY"}, {"entities", json::array({rect})}});
    check(sketch.contains("sketch"), QStringLiteral("create_sketch"));
    const json ex = tool("extrude", {{"sketch", sketch.value("sketch", 0)}, {"distance", 10}});
    check(ex.contains("bodies") && std::fabs(ex["bodies"][0].value("volume_mm3", 0.0) - 28000.0) < 0.01, QStringLiteral("extrude: 70 x 40 x 10"));
    json corners = json::array();
    for(const auto &e : tool("list_edges", {{"body", "Body1"}, {"direction", "z"}}).value("edges", json::array()))
        corners.push_back({{"body", "Body1"}, {"index", e["index"]}});
    check(tool("fillet", {{"edges", corners}, {"radius", 6}}).value("status", "") == "ok", QStringLiteral("fillet the 4 corners"));
    const json top = tool("list_faces", {{"body", "Body1"}, {"normal", "+z"}});
    const json holes = tool("hole", {{"face", {{"body", "Body1"}, {"index", top["faces"][0]["index"]}}},
                                     {"points", {{12, 20, 10}, {35, 20, 10}, {58, 20, 10}}},
                                     {"type", "countersink"},
                                     {"diameter", 3.4},
                                     {"through_all", true}});
    check(holes.value("status", "") == "ok", QStringLiteral("three countersunk holes"));
    const json stl = tool("export_stl", {{"path", out.absoluteFilePath(QStringLiteral("mcp_plate.stl")).toStdString()}});
    check(stl.contains("report") && stl["report"].value("printable", false), QStringLiteral("export_stl: printable"));
    vp->setStandardView(StandardView::Home, false);
    vp->fitAll(false);
    // Let the glow pulse, then look at the window and the button.
    processEventsFor(900);
    waitForFrames(vp, 2);
    check(w.mcpButton()->glow() > 0.05, QStringLiteral("the button glows (%1)").arg(w.mcpButton()->glow(), 0, 'f', 2));
    w.grab().save(out.filePath(QStringLiteral("mcp_1_connected.png")));
    const QPoint corner = w.mcpButton()->mapTo(&w, QPoint(0, 0));
    w.grab(QRect(corner - QPoint(120, 6), QSize(w.mcpButton()->width() + 128, w.mcpButton()->height() + 12)))
        .scaled(3 * (w.mcpButton()->width() + 128), 3 * (w.mcpButton()->height() + 12), Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .save(out.filePath(QStringLiteral("mcp_2_button.png")));
    McpDialog *dlg = w.openMcpDialog();
    processEventsFor(200);
    check(dlg->logView()->topLevelItemCount() >= 10, QStringLiteral("the event log lists the calls (%1 rows)").arg(dlg->logView()->topLevelItemCount()));
    dlg->grab().save(out.filePath(QStringLiteral("mcp_3_event_log.png")));
    if(auto *tabs = dlg->findChild<QTabWidget *>()) tabs->setCurrentIndex(0);
    processEventsFor(100);
    check(dlg->statusLabel()->text().contains(QStringLiteral("Connected")), QStringLiteral("the dialog shows the connection"));
    dlg->grab().save(out.filePath(QStringLiteral("mcp_4_settings.png")));
    check(QFileInfo(w.mcpLog()->filePath()).size() > 0 && QFileInfo(w.mcpLog()->filePath()).size() <= McpLog::maxBytes(),
          QStringLiteral("the log file is written (%1)").arg(w.mcpLog()->filePath()));
    dlg->close();
    s.enabled = false;
    w.mcpServer()->apply(s);
    check(w.mcpButton()->state() == McpServer::State::Off, QStringLiteral("switched off again"));
    return ok;
}


// The icon set on contact sheets: every icon at the sizes the UI uses, and
// large, to review; each must render something at 16 px.
bool iconsScenario(MainWindow &w, const QDir &out, QTextStream &log) {
    bool ok = true;
    auto check = [&](bool c, const QString &what) {
        log << (c ? "  ok   " : "  FAIL ") << what << "\n";
        ok = ok && c;
    };
    const std::vector<IconId> ids = allIcons();
    const int sizes[] = {16, 20, 24, 32, 48};
    const int cellW = 16 + 20 + 24 + 32 + 48 + 6 * 8 + 150, cellH = 64;
    const int cols = 3, rows = int((ids.size() + cols - 1) / cols);
    QImage sheet(cols * cellW, rows * cellH, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(0xF4, 0xF6, 0xF9));
    QImage large(8 * 144, int((ids.size() + 7) / 8) * 160, QImage::Format_ARGB32_Premultiplied);
    large.fill(QColor(0xF4, 0xF6, 0xF9));
    QPainter p(&sheet), pl(&large);
    QFont font = p.font();
    font.setPixelSize(12);
    p.setFont(font);
    pl.setFont(font);
    int blank = 0;
    for(size_t i = 0; i < ids.size(); ++i) {
        const int cx = int(i % cols) * cellW, cy = int(i / cols) * cellH;
        if(i / cols % 2) p.fillRect(cx, cy, cellW, cellH, QColor(0xEC, 0xF0, 0xF5));
        p.setPen(QColor(0x40, 0x4C, 0x5C));
        p.drawText(QRect(cx + 8, cy, 142, cellH), Qt::AlignVCenter, iconName(ids[i]));
        int x = cx + 150;
        for(int sz : sizes) {
            const QImage img = iconImage(ids[i], sz);
            p.drawImage(x, cy + (cellH - sz) / 2, img);
            x += sz + 8;
            if(sz == 16) {
                int opaque = 0;
                for(int yy = 0; yy < img.height(); ++yy)
                    for(int xx = 0; xx < img.width(); ++xx) opaque += qAlpha(img.pixel(xx, yy)) > 128;
                if(opaque < 12) {
                    ++blank;
                    log << "         " << iconName(ids[i]) << " is almost empty at 16 px\n";
                }
            }
        }
        const int lx = int(i % 8) * 144, ly = int(i / 8) * 160;
        pl.drawImage(lx + 8, ly + 6, iconImage(ids[i], 128));
        pl.setPen(QColor(0x40, 0x4C, 0x5C));
        pl.drawText(QRect(lx, ly + 136, 144, 20), Qt::AlignCenter, iconName(ids[i]));
    }
    p.end();
    pl.end();
    sheet.save(out.filePath(QStringLiteral("icons_sheet.png")));
    large.save(out.filePath(QStringLiteral("icons_large.png")));
    check(blank == 0, QStringLiteral("all %1 icons render at 16 px").arg(ids.size()));

    // The icons in place, at 3x: ribbon, browser, timeline and the view cube.
    buildDemoBracket(w.document());
    w.waitForModel(60000);
    w.viewport()->setStandardView(StandardView::Home, false);
    w.viewport()->fitAll(false);
    waitForFrames(w.viewport(), 2);
    processEventsFor(100);
    auto zoomShot = [&](QWidget *wid, QRect area, const char *name) {
        const QPixmap pm = wid->grab(area.isNull() ? wid->rect() : area);
        pm.toImage()
            .scaled(pm.width() * 3, pm.height() * 3, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(out.filePath(QString::fromLatin1(name)));
    };
    zoomShot(w.ribbon(), QRect(0, 0, std::min(560, w.ribbon()->width()), w.ribbon()->height()), "icons_ribbon.png");
    // A group caption hovered, and its menu open.
    MenuButton *create = nullptr;
    for(MenuButton *c : w.ribbon()->findChildren<MenuButton *>(QStringLiteral("ribbonGroupCaption")))
        if(!create && c->isVisible()) create = c;
    if(create) {
        check(!create->text().contains(QChar(0x25BE)), QStringLiteral("captions have no text chevron"));
        QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), create->mapToGlobal(QPointF(5, 5)));
        QCoreApplication::sendEvent(create, &enter);
        zoomShot(w.ribbon(), QRect(0, 0, std::min(560, w.ribbon()->width()), w.ribbon()->height()), "icons_ribbon_hover.png");
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(create, &leave);
        QMenu *menu = create->menu();
        menu->popup(create->mapToGlobal(QPoint(0, create->height())));
        processEventsFor(150);
        check(create->isOpen(), QStringLiteral("the caption shows its menu is open"));
        zoomShot(w.ribbon(), QRect(0, 0, std::min(560, w.ribbon()->width()), w.ribbon()->height()), "icons_ribbon_open.png");
        const QPixmap pm = menu->grab();
        pm.toImage().scaled(pm.width() * 3, pm.height() * 3, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(out.filePath(QStringLiteral("icons_menu.png")));
        menu->hide();
        processEventsFor(50);
    }
    zoomShot(w.browser(), QRect(0, 0, w.browser()->width(), std::min(200, w.browser()->height())), "icons_browser.png");
    zoomShot(w.timeline(), QRect(0, 0, std::min(560, w.timeline()->width()), w.timeline()->height()),
             "icons_timeline.png");
    Viewport *vp = w.viewport();
    const QRect cube = ViewCube::rect(vp->size()).adjusted(-16, -10, 10, 16);
    auto cubeShot = [&](const char *name) {
        waitForFrames(vp, 2);
        processEventsFor(60);
        w.grab(QRect(vp->mapTo(&w, cube.topLeft()), cube.size()))
            .toImage()
            .scaled(cube.width() * 4, cube.height() * 4, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(out.filePath(QString::fromLatin1(name)));
    };
    cubeShot("icons_viewcube.png");
    // Hovering an edge of the cube.
    {
        const QMatrix4x4 m = ViewCube::viewProjection(vp->camera().rotation);
        const QVector3D ndc = m.map(QVector3D(0.95f, -0.95f, 0.0f)); // FRONT/RIGHT edge
        const QRect r = ViewCube::rect(vp->size());
        const QPointF px(r.left() + (ndc.x() + 1) * 0.5 * r.width(), r.top() + (1 - ndc.y()) * 0.5 * r.height());
        sendMouse(vp, QEvent::MouseMove, px, Qt::NoButton, Qt::NoButton);
        cubeShot("icons_viewcube_hover_edge.png");
    }
    // Square to the front: the turn and roll arrows appear.
    vp->setStandardView(StandardView::Front, false);
    sendMouse(vp, QEvent::MouseMove, vp->cubeControlShape(Viewport::CubeControl::Right).boundingRect().center(),
              Qt::NoButton, Qt::NoButton);
    check(vp->cubeFaceOn(), QStringLiteral("the front view is square to a face"));
    cubeShot("icons_viewcube_front.png");
    sendMouse(vp, QEvent::MouseMove, QPointF(10, vp->height() / 2.0), Qt::NoButton, Qt::NoButton);
    vp->setStandardView(StandardView::Home, false);
    w.grab().save(out.filePath(QStringLiteral("icons_window.png")));
    return ok;
}

const std::map<QString, Scenario> &scenarios() {
    static const std::map<QString, Scenario> s = {
        {QStringLiteral("smoke"), smokeScenario},
        {QStringLiteral("views"), viewsScenario},
        {QStringLiteral("sketch"), sketchScenario},
        {QStringLiteral("plate"), plateScenario},
        {QStringLiteral("features"), featuresScenario},
        {QStringLiteral("section"), sectionScenario},
        {QStringLiteral("acceptance"), acceptanceScenario},
        {QStringLiteral("mcp"), mcpScenario},
        {QStringLiteral("icons"), iconsScenario},
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
