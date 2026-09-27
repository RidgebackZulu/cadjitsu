// Sketch mode: creating sketches on planes and faces, the drawing tools with
// snapping and heads-up input, dimensions, constraints, selection, dragging,
// undo, and committing the sketch into the document.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "model/ModelView.h"
#include "selftest/DemoModels.h"
#include "selftest/TestUtil.h"
#include "sketch/HeadsUpInput.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "ui/Ribbon.h"
#include "viewport/Viewport.h"

#include "features/ExtrudeFeature.h"
#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"

#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QtTest>

#include <algorithm>

using namespace cadly;
using cad::SkCon;
using cad::SkType;
using cad::Vec2;

namespace {

void send(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
          Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, mods);
    QCoreApplication::sendEvent(w, &ev);
}

int countType(const cad::Sketch &s, SkType t) {
    return int(std::count_if(s.entities.begin(), s.entities.end(), [&](const cad::SkEntity &e) { return e.type == t; }));
}

int countCon(const cad::Sketch &s, SkCon t) {
    return int(
        std::count_if(s.constraints.begin(), s.constraints.end(), [&](const cad::SkConstraint &c) { return c.type == t; }));
}

const cad::SkConstraint *findCon(const cad::Sketch &s, SkCon t) {
    for(const auto &c : s.constraints)
        if(c.type == t) return &c;
    return nullptr;
}

double lineLength(const cad::Sketch &s, const cad::SkEntity &e) { return distance(s.pointPos(e.a), s.pointPos(e.b)); }

} // namespace

class SketchTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;

    Viewport *vp() { return m_window->viewport(); }
    SketchMode *mode() { return m_window->sketchMode(); }
    SketchEditor *ed() { return mode()->editor(); }
    const cad::Sketch &sk() { return ed()->sketch(); }

    QPointF at(double x, double y) { return ed()->toScreen({x, y}); }

    void move(QPointF p) { send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton); }
    void click(QPointF p, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        move(p);
        send(vp(), QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, mods);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, mods);
    }
    void doubleClick(QPointF p) {
        click(p);
        send(vp(), QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    }
    void dragLeft(QPointF from, QPointF to) {
        move(from);
        send(vp(), QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for(int i = 1; i <= 8; ++i) send(vp(), QEvent::MouseMove, from + (to - from) * (i / 8.0), Qt::NoButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    }
    // Types into whichever widget has keyboard focus (the canvas, a heads-up box...).
    void type(const QString &text) {
        for(const QChar c : text) {
            QWidget *target = m_window->focusWidget() ? m_window->focusWidget() : vp();
            QTest::keyClick(target, c.toLatin1());
        }
    }
    void key(Qt::Key k) {
        QWidget *target = m_window->focusWidget() ? m_window->focusWidget() : vp();
        QTest::keyClick(target, k);
    }
    void trigger(const char *action) {
        QAction *a = m_window->action(QString::fromLatin1(action));
        QVERIFY2(a, action);
        QVERIFY2(a->isEnabled(), action);
        a->trigger();
    }
    void startSketchOnXY() {
        QVERIFY(mode()->beginNewSketch(cad::PlaneRef::origin(cad::PlaneRef::Kind::XY), false));
        QVERIFY(mode()->active());
        QVERIFY(waitForFrames(vp(), 1));
    }

private slots:
    void init() {
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1200, 800);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_window->activateWindow();
        vp()->setFocus();
        QVERIFY(waitForFrames(vp(), 2));
    }

    void cleanup() { m_window.reset(); }

    void createSketchPicksAnOriginPlane() {
        vp()->setStandardView(StandardView::Top, false);
        QVERIFY(waitForFrames(vp(), 1));
        trigger("createSketch");
        QVERIFY(mode()->pickingPlane());
        // The XY plane square spans the positive quadrant near the origin.
        const float s = vp()->camera().viewHeightAtTarget() * 0.2f;
        const QPointF p = vp()->camera().project(QVector3D(s * 0.5f, s * 0.5f, 0));
        move(p);
        click(p);
        QVERIFY(mode()->active());
        QVERIFY(!mode()->pickingPlane());
        QCOMPARE(int(m_window->document().features().size()), 1);
        const auto f = std::dynamic_pointer_cast<const cad::SketchFeature>(m_window->document().features()[0]);
        QVERIFY(f);
        QCOMPARE(f->plane.kind, cad::PlaneRef::Kind::XY);
        QVERIFY(m_window->ribbon()->currentTab() == m_window->sketchTab());
        processEventsFor(450); // look-at animation
        QVERIFY(vp()->camera().forward().z() < -0.999f);
        QVERIFY(m_window->action(QStringLiteral("finishSketch"))->isEnabled());
        QVERIFY(!m_window->action(QStringLiteral("createSketch"))->isEnabled());
    }

    void createSketchOnAPlanarFace() {
        buildDemoBracket(m_window->document());
        m_window->refresh();
        vp()->setStandardView(StandardView::Top, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 2));
        trigger("createSketch");
        click(vp()->camera().project(QVector3D(30, 8, 12))); // plate top face
        QVERIFY(mode()->active());
        const auto f = std::dynamic_pointer_cast<const cad::SketchFeature>(m_window->document().features().back());
        QCOMPARE(f->plane.kind, cad::PlaneRef::Kind::Face);
        const gp_Ax3 &fr = ed()->frame();
        QVERIFY(fr.Direction().IsEqual(gp_Dir(0, 0, 1), 1e-9));
        QVERIFY(std::fabs(fr.Location().Z() - 12.0) < 1e-9);
        // Sketch coordinates map onto the face: (30, 8) is where we clicked.
        const QPointF p = at(30, 8), q = vp()->camera().project(QVector3D(30, 8, 12));
        QVERIFY(QLineF(p, q).length() < 1.0);
    }

    void rectangleWithTypedWidthAndHeight() {
        startSketchOnXY();
        trigger("sketchRectangle");
        QCOMPARE(mode()->tool(), SketchToolKind::Rectangle);
        click(at(0, 0)); // snaps to the origin
        move(at(30, 18));
        type(QStringLiteral("40"));
        QVERIFY(mode()->hud().locked(0));
        key(Qt::Key_Tab);
        type(QStringLiteral("25"));
        QVERIFY(mode()->hud().locked(1));
        key(Qt::Key_Return);

        QCOMPARE(countType(sk(), SkType::Line), 4);
        QCOMPARE(countType(sk(), SkType::Point), 4);
        QCOMPARE(countCon(sk(), SkCon::Horizontal), 2);
        QCOMPARE(countCon(sk(), SkCon::Vertical), 2);
        QCOMPARE(countCon(sk(), SkCon::Coincident), 1);
        QCOMPARE(countCon(sk(), SkCon::Distance), 2);
        std::vector<std::string> exprs;
        for(const auto &c : sk().constraints)
            if(c.type == SkCon::Distance) exprs.push_back(c.expr);
        std::sort(exprs.begin(), exprs.end());
        QCOMPARE(exprs, (std::vector<std::string>{"25 mm", "40 mm"}));
        QCOMPARE(int(ed()->profiles().size()), 1);
        QVERIFY(std::fabs(std::fabs(ed()->profiles()[0].area) - 1000.0) < 1e-6);
        QCOMPARE(ed()->solveResult().dof, 0);
        QVERIFY(ed()->solveResult().freeEntities.empty());
        QVERIFY(!mode()->hud().visible());
    }

    void lineChainWithInferenceClosesAProfile() {
        startSketchOnXY();
        trigger("sketchLine");
        click(at(10, 10));
        click(at(50, 10.4)); // within 3 degrees of horizontal
        click(at(50.3, 40)); // nearly vertical
        click(at(10, 40.2));
        click(at(10, 10)); // back to the first point: closes and ends the chain
        QCOMPARE(countType(sk(), SkType::Line), 4);
        QCOMPARE(countType(sk(), SkType::Point), 4);
        QCOMPARE(countCon(sk(), SkCon::Horizontal), 2);
        QCOMPARE(countCon(sk(), SkCon::Vertical), 1 + 1);
        QCOMPARE(int(ed()->profiles().size()), 1);
        for(const auto &e : sk().entities)
            if(e.type == SkType::Line) {
                const Vec2 a = sk().pointPos(e.a), b = sk().pointPos(e.b);
                QVERIFY(std::fabs(a.x - b.x) < 1e-6 || std::fabs(a.y - b.y) < 1e-6);
            }
        // The chain ended: the next click starts a new line.
        click(at(-20, -20));
        QCOMPARE(countType(sk(), SkType::Line), 4);
        key(Qt::Key_Escape); // abandon the new start point
        key(Qt::Key_Escape); // back to Select
        QCOMPARE(mode()->tool(), SketchToolKind::Select);
    }

    void lineLengthFromHeadsUpInput() {
        startSketchOnXY();
        trigger("sketchLine");
        click(at(5, 5));
        move(at(25, 5));
        type(QStringLiteral("32.5"));
        key(Qt::Key_Return);
        key(Qt::Key_Escape);
        QCOMPARE(countType(sk(), SkType::Line), 1);
        const cad::SkEntity *line = nullptr;
        for(const auto &e : sk().entities)
            if(e.type == SkType::Line) line = &e;
        QVERIFY(line);
        QVERIFY(std::fabs(lineLength(sk(), *line) - 32.5) < 1e-6);
        const cad::SkConstraint *d = findCon(sk(), SkCon::Distance);
        QVERIFY(d);
        QCOMPARE(d->expr, std::string("32.5 mm"));
        QCOMPARE(countCon(sk(), SkCon::Horizontal), 1);
    }

    void circleWithTypedDiameter() {
        startSketchOnXY();
        trigger("sketchCircle");
        click(at(20, 15));
        move(at(26, 15));
        type(QStringLiteral("12"));
        key(Qt::Key_Return);
        QCOMPARE(countType(sk(), SkType::Circle), 1);
        const cad::SkEntity *c = nullptr;
        for(const auto &e : sk().entities)
            if(e.type == SkType::Circle) c = &e;
        QVERIFY(std::fabs(c->r - 6.0) < 1e-6);
        const cad::SkConstraint *d = findCon(sk(), SkCon::Diameter);
        QVERIFY(d);
        QCOMPARE(d->expr, std::string("12 mm"));
        QCOMPARE(int(ed()->profiles().size()), 1);
    }

    void dimensionToolPlacesAndEditsAValue() {
        startSketchOnXY();
        trigger("sketchLine");
        click(at(0, 20));
        click(at(30, 35));
        key(Qt::Key_Escape);
        trigger("sketchDimension");
        click(at(15, 27.5)); // the line
        click(at(13, 31));   // place the label close beside the line: aligned
        auto *box = qobject_cast<InlineValueEditor *>(m_window->focusWidget());
        QVERIFY2(box, "the value box opens after placing a dimension");
        box->selectAll();
        type(QStringLiteral("50"));
        key(Qt::Key_Return);
        processEventsFor(20);
        const cad::SkConstraint *d = findCon(sk(), SkCon::Distance);
        QVERIFY(d);
        QCOMPARE(d->expr, std::string("50 mm"));
        const int dimId = d->id;
        for(const auto &e : sk().entities)
            if(e.type == SkType::Line) QVERIFY(std::fabs(lineLength(sk(), e) - 50.0) < 1e-6);

        // Double-clicking the label edits it again; expressions work.
        trigger("sketchDimension");
        key(Qt::Key_Escape);
        QCOMPARE(mode()->tool(), SketchToolKind::Select);
        QVERIFY(waitForFrames(vp(), 1));
        const auto rect = ed()->dimensionRect(dimId);
        QVERIFY(rect);
        doubleClick(rect->center());
        box = qobject_cast<InlineValueEditor *>(m_window->focusWidget());
        QVERIFY(box);
        box->selectAll();
        type(QStringLiteral("2*20"));
        key(Qt::Key_Return);
        processEventsFor(20);
        for(const auto &e : sk().entities)
            if(e.type == SkType::Line) QVERIFY(std::fabs(lineLength(sk(), e) - 40.0) < 1e-6);
    }

    void dimensionOrientationFollowsTheCursor() {
        startSketchOnXY();
        cad::Sketch s;
        const int p1 = s.addPoint(0, 0), p2 = s.addPoint(20, 30);
        ed()->setSketch(s);
        auto place = [&](Vec2 label) {
            trigger("sketchDimension");
            click(at(0, 0));
            click(at(20, 30));
            click(at(label.x, label.y));
            if(auto *box = mode()->dimensionEditor()) box->dismiss();
            processEventsFor(10);
            return sk().constraints.back();
        };
        const cad::SkConstraint below = place({10, -10});
        QCOMPARE(below.type, SkCon::HDistance);
        QVERIFY(std::fabs(ed()->measuredValue(below) - 20.0) < 1e-9);
        const cad::SkConstraint right = place({35, 15});
        QCOMPARE(right.type, SkCon::VDistance);
        QVERIFY(std::fabs(ed()->measuredValue(right) - 30.0) < 1e-9);
        QCOMPARE(sk().constraints.size(), size_t(2));
        Q_UNUSED(p1);
        Q_UNUSED(p2);
    }

    void angleDimensionBetweenTwoLines() {
        startSketchOnXY();
        cad::Sketch s;
        const int l1 = s.addLine(Vec2{0, 0}, Vec2{40, 0});
        const int l2 = s.addLine(Vec2{0, 0}, Vec2{30, 30});
        s.addConstraint(SkCon::Coincident, s.find(l1)->a, s.find(l2)->a);
        s.addConstraint(SkCon::Horizontal, l1);
        ed()->setSketch(s);
        trigger("sketchDimension");
        click(at(30, 0));
        click(at(15, 15));
        click(at(25, 8)); // inside the angle
        auto *box = qobject_cast<InlineValueEditor *>(m_window->focusWidget());
        QVERIFY(box);
        const cad::SkConstraint *a = findCon(sk(), SkCon::Angle);
        QVERIFY(a);
        QVERIFY(std::fabs(ed()->measuredValue(*a) - cad::kPi / 4) < 1e-6);
        box->selectAll();
        type(QStringLiteral("30"));
        key(Qt::Key_Return);
        processEventsFor(20);
        const cad::SkEntity *e2 = sk().find(l2);
        const Vec2 d = (sk().pointPos(e2->b) - sk().pointPos(e2->a)).normalized();
        QVERIFY(std::fabs(std::atan2(d.y, d.x) - cad::kPi / 6) < 1e-6);
        QVERIFY(waitForFrames(vp(), 1));
        QVERIFY(ed()->dimensionRect(findCon(sk(), SkCon::Angle)->id));
    }

    void arcToolAndTangentConstraint() {
        startSketchOnXY();
        trigger("sketchArc");
        click(at(0, 0));
        click(at(20, 0));
        click(at(10, 10)); // through the top: a half circle of radius 10
        QCOMPARE(countType(sk(), SkType::Arc), 1);
        const cad::SkEntity *arc = nullptr;
        for(const auto &e : sk().entities)
            if(e.type == SkType::Arc) arc = &e;
        QVERIFY(std::fabs(sk().arcRadius(*arc) - 10.0) < 0.05);
        const int arcId = arc->id;
        key(Qt::Key_Escape);
        // A line from the arc's end, then make it tangent.
        trigger("sketchLine");
        click(at(20, 0));
        click(at(28, -15));
        key(Qt::Key_Escape);
        key(Qt::Key_Escape);
        int lineId = 0;
        for(const auto &e : sk().entities)
            if(e.type == SkType::Line) lineId = e.id;
        QVERIFY(lineId);
        trigger("constraintTangent");
        const cad::SkEntity *ln = sk().find(lineId);
        const Vec2 lm = (sk().pointPos(ln->a) + sk().pointPos(ln->b)) * 0.5;
        click(at(lm.x, lm.y));
        click(at(10, 10));
        QCOMPARE(countCon(sk(), SkCon::Tangent), 1);
        // Tangent at the shared end: the line is perpendicular to the radius there.
        ln = sk().find(lineId);
        const cad::SkEntity *a2 = sk().find(arcId);
        const Vec2 c = sk().pointPos(a2->a);
        const Vec2 end = sk().pointPos(ln->a);
        const Vec2 dir = (sk().pointPos(ln->b) - end).normalized();
        QVERIFY(std::fabs(dir.dot((end - c).normalized())) < 1e-6);
    }

    void overConstrainingDimensionBecomesDriven() {
        startSketchOnXY();
        cad::Sketch s;
        const auto rect = s.addRectangle({0, 0}, {40, 20});
        ed()->setSketch(s);
        QVERIFY(ed()->addDimension(SkCon::Distance, rect[0], 0, {0, -5}) > 0);
        bool driven = false;
        const int id = ed()->addDimension(SkCon::Distance, rect[2], 0, {0, 5}, false, &driven);
        QVERIFY(driven);
        QVERIFY(sk().findConstraint(id)->driven);
        QVERIFY(ed()->solveResult().ok);
        // Editing a driven dimension to a conflicting value is refused.
        QString error;
        QVERIFY(!ed()->setDimensionExpression(id, QStringLiteral("30"), &error));
        QVERIFY(!error.isEmpty());
    }

    void constraintToolsRelateGeometry() {
        startSketchOnXY();
        cad::Sketch s;
        const int l1 = s.addLine(Vec2{0, 0}, Vec2{30, 5});
        const int l2 = s.addLine(Vec2{0, 20}, Vec2{25, 32});
        ed()->setSketch(s);
        trigger("constraintParallel");
        click(at(15, 2.5));
        click(at(12.5, 26));
        QCOMPARE(countCon(sk(), SkCon::Parallel), 1);
        auto dir = [&](int id) {
            const cad::SkEntity *e = sk().find(id);
            return (sk().pointPos(e->b) - sk().pointPos(e->a)).normalized();
        };
        QVERIFY(std::fabs(dir(l1).cross(dir(l2))) < 1e-9);

        // With a selection, a constraint applies at once.
        trigger("constraintHorizontalVertical");
        key(Qt::Key_Escape);
        ed()->selectedEntities = {l1};
        trigger("constraintHorizontalVertical");
        QCOMPARE(countCon(sk(), SkCon::Horizontal), 1);
        QVERIFY(std::fabs(dir(l1).y) < 1e-9 && std::fabs(dir(l2).y) < 1e-9);

        // A redundant constraint is refused.
        auto mid = [&](int id) {
            const cad::SkEntity *e = sk().find(id);
            return (sk().pointPos(e->a) + sk().pointPos(e->b)) * 0.5;
        };
        trigger("constraintParallel");
        const auto before = sk().constraints.size();
        const Vec2 m1 = mid(l1), m2 = mid(l2);
        click(at(m1.x, m1.y));
        click(at(m2.x, m2.y));
        QCOMPARE(sk().constraints.size(), before);
    }

    void selectShowsStatsAndDragsPoints() {
        startSketchOnXY();
        cad::Sketch s;
        const int line = s.addLine(Vec2{0, 0}, Vec2{30, 40});
        ed()->setSketch(s);
        QVERIFY(waitForFrames(vp(), 1));
        click(at(15, 20));
        QCOMPARE(ed()->selectedEntities, std::set<int>{line});
        const QString stats = m_window->selectionStatsLabel()->text();
        QVERIFY2(stats.contains(QStringLiteral("Line")) && stats.contains(QStringLiteral("50.00")), qPrintable(stats));

        // Drag the end point; undo puts it back.
        const int b = sk().find(line)->b;
        dragLeft(at(30, 40), at(40, 45));
        QVERIFY(distance(sk().pointPos(b), Vec2(40, 45)) < 0.05);
        trigger("undo");
        QVERIFY(distance(sk().pointPos(b), Vec2(30, 40)) < 1e-9);
        trigger("redo");
        QVERIFY(distance(sk().pointPos(b), Vec2(40, 45)) < 0.05);

        // Box selection (left to right = window) and delete.
        click(at(70, -50)); // empty: clears the selection
        QVERIFY(ed()->selectedEntities.empty());
        dragLeft(at(-10, -10) + QPointF(0, 0), at(50, 60));
        QCOMPARE(ed()->selectedEntities, std::set<int>{line});
        key(Qt::Key_Delete);
        QCOMPARE(countType(sk(), SkType::Line), 0);
    }

    void draggingASelectionMovesItAndTheBodyFollowsLive() {
        cad::Document &doc = m_window->document();
        MainWindow::setLiveSketchBodies(true);
        // A rectangle extruded into a body.
        auto s = std::make_shared<cad::SketchFeature>();
        s->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        const std::vector<int> lines = s->sketch.addRectangle({0, 0}, {20, 10});
        const cad::FeatureId sid = doc.addFeature(s);
        auto e = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &p : doc.stateAt(1)->sketches.at(sid)->profiles) e->profiles.push_back({sid, p.key, p.sample});
        e->distance = doc.makeSlot("5 mm");
        doc.addFeature(e);
        m_window->refresh();
        QVERIFY(m_window->waitForModel());
        auto bodyMinX = [&] {
            const cad::StatePtr st = m_window->modelView()->state();
            if(!st || st->bodies.empty()) return -1e9;
            double x0, y0, z0, x1, y1, z1;
            cad::boundingBox(st->bodies.begin()->second->shape.shape()).Get(x0, y0, z0, x1, y1, z1);
            return x0;
        };
        QVERIFY(std::fabs(bodyMinX()) < 1e-6);
        QVERIFY(mode()->editSketch(sid, false));
        QVERIFY(waitForFrames(vp(), 1));

        // Select the whole rectangle and drag one side: all of it moves.
        ed()->selectEntities({lines.begin(), lines.end()}, false);
        dragLeft(at(0, 5), at(12, 5));
        for(const auto &en : sk().entities)
            if(en.type == SkType::Point && en.id > 0) QVERIFY2(en.x > 11.9, qPrintable(QString::number(en.x)));
        // The body followed before Finish Sketch.
        QTRY_VERIFY_WITH_TIMEOUT(std::fabs(bodyMinX() - 12.0) < 0.05, 5000);
        QVERIFY(mode()->active());

        // Esc in the middle of a drag puts it back.
        const cad::Sketch before = sk();
        move(at(12, 5));
        send(vp(), QEvent::MouseButtonPress, at(12, 5), Qt::LeftButton, Qt::LeftButton);
        for(int i = 1; i <= 4; ++i) send(vp(), QEvent::MouseMove, at(12 + 2 * i, 5), Qt::NoButton, Qt::LeftButton);
        QTest::keyClick(vp(), Qt::Key_Escape);
        send(vp(), QEvent::MouseButtonRelease, at(20, 5), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(sk().toJson(), before.toJson());

        // Move / Copy: a typed shift, then a copy placed with Ctrl.
        trigger("sketchMove");
        click(at(12, 0)); // the base point: a corner
        type(QStringLiteral("5"));
        key(Qt::Key_Return);
        for(const auto &en : sk().entities)
            if(en.type == SkType::Point && en.id > 0) QVERIFY2(en.x > 16.9, qPrintable(QString::number(en.x)));
        click(at(17, 0));
        click(at(17, 30), Qt::ControlModifier);
        QCOMPARE(countType(sk(), SkType::Line), 8);
        QCOMPARE(int(ed()->profiles().size()), 2);
        // The copy keeps the rectangle's horizontal and vertical constraints.
        QCOMPARE(countCon(sk(), SkCon::Horizontal) + countCon(sk(), SkCon::Vertical), 8);

        trigger("finishSketch");
        QVERIFY(m_window->waitForModel());
        // The extrude still finds its (moved) rectangle.
        QVERIFY(std::fabs(bodyMinX() - 17.0) < 0.05);
        QVERIFY(doc.statusOf(e->id).severity != cad::Severity::Error);
    }

    void offsetShellsARectangle() {
        cad::Document &doc = m_window->document();
        QVERIFY(m_window->action(QStringLiteral("sketchOffset"))->toolTip().contains(
            QStringLiteral("Copies the selected sketch curves")));
        startSketchOnXY();
        trigger("sketchRectangle");
        click(at(0, 0));
        click(at(20, 10));
        key(Qt::Key_Escape);
        QCOMPARE(countType(sk(), SkType::Line), 4);

        // One click on an edge takes the whole outline; the preview follows the mouse.
        trigger("sketchOffset");
        click(at(20, 5));
        QCOMPARE(int(ed()->selectedEntities.size()), 4);
        move(at(17, 5));
        QCOMPARE(int(ed()->previewLines.size()), 4);
        // Esc lets go of it.
        key(Qt::Key_Escape);
        QVERIFY(ed()->selectedEntities.empty());
        QVERIFY(ed()->previewLines.empty());

        // Too big: nothing is made.
        click(at(20, 5));
        move(at(17, 5));
        type(QStringLiteral("6"));
        key(Qt::Key_Return);
        QCOMPARE(countType(sk(), SkType::Line), 4);
        key(Qt::Key_Escape);

        // Inside by a typed 2 mm.
        click(at(0, 5));
        move(at(3, 5));
        type(QStringLiteral("2"));
        key(Qt::Key_Return);
        QCOMPARE(countType(sk(), SkType::Line), 8);
        QCOMPARE(int(ed()->profiles().size()), 2);
        auto innerBox = [&](double &x0, double &y0, double &x1, double &y1) {
            x0 = y0 = 1e9;
            x1 = y1 = -1e9;
            for(const auto &e : sk().entities)
                if(e.type == SkType::Point && e.x > 0.5 && e.x < 19.5 && e.y > 0.5 && e.y < 9.5) {
                    x0 = std::min(x0, e.x);
                    y0 = std::min(y0, e.y);
                    x1 = std::max(x1, e.x);
                    y1 = std::max(y1, e.y);
                }
        };
        double x0, y0, x1, y1;
        innerBox(x0, y0, x1, y1);
        QVERIFY2(std::fabs(x0 - 2) < 1e-3 && std::fabs(y0 - 2) < 1e-3 && std::fabs(x1 - 18) < 1e-3 && std::fabs(y1 - 8) < 1e-3,
                 qPrintable(QStringLiteral("%1 %2 %3 %4 %5").arg(x0).arg(y0).arg(x1).arg(y1).arg(QString::fromStdString(sk().toJson().dump()))));
        // One offset dimension on the canvas, driving all four sides.
        const cad::SkConstraint *dim = nullptr;
        for(const auto &c : sk().constraints)
            if(cad::isDimension(c.type) && !c.valueFrom) dim = &c;
        QVERIFY(dim);
        QCOMPARE(QString::fromStdString(dim->expr), QStringLiteral("2 mm"));
        const int dimId = dim->id;
        QVERIFY(ed()->dimensionRect(dimId).has_value() || waitForFrames(vp(), 1));
        QVERIFY(ed()->dimensionRect(dimId).has_value());
        // Changing it moves the copy, not the original.
        QVERIFY(ed()->setDimensionExpression(dimId, QStringLiteral("3")));
        innerBox(x0, y0, x1, y1);
        QVERIFY2(std::fabs(x0 - 3) < 1e-3 && std::fabs(y1 - 7) < 1e-3, qPrintable(QStringLiteral("%1 %2 %3").arg(x0).arg(y1).arg(QString::fromStdString(sk().toJson()["entities"].dump()))));
        for(const auto &e : sk().entities)
            if(e.type == SkType::Point && (e.x < 0.5 || e.x > 19.5))
                QVERIFY(std::fabs(e.x) < 1e-3 || std::fabs(e.x - 20) < 1e-3);

        // Finished and extruded, the ring between the outlines is a 3 mm wall.
        trigger("finishSketch");
        QVERIFY(m_window->waitForModel());
        const cad::FeatureId sid = doc.features().back()->id;
        const auto &profiles = doc.stateAt(doc.marker())->sketches.at(sid)->profiles;
        auto e = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &p : profiles)
            if(!p.holes.empty()) e->profiles.push_back({sid, p.key, p.sample});
        QCOMPARE(int(e->profiles.size()), 1);
        e->distance = doc.makeSlot("5 mm");
        doc.addFeature(e);
        m_window->refresh();
        QVERIFY(m_window->waitForModel());
        const cad::StatePtr st = m_window->modelView()->state();
        QCOMPARE(int(st->bodies.size()), 1);
        QVERIFY(std::fabs(cad::volumeOf(st->bodies.begin()->second->shape.shape()) - (200.0 - 14.0 * 4.0) * 5.0) < 1e-3);
    }

    void constructionToggleAndProfiles() {
        startSketchOnXY();
        trigger("sketchRectangle");
        click(at(0, 0));
        click(at(20, 10));
        QCOMPARE(int(ed()->profiles().size()), 1);
        key(Qt::Key_Escape);
        ed()->selectEntities(ed()->entitiesInRect(QRectF(at(-5, 15), at(25, -5)).normalized(), false), false);
        QCOMPARE(int(ed()->selectedEntities.size()), 4);
        trigger("sketchConstruction");
        QCOMPARE(int(ed()->profiles().size()), 0);
        for(const auto &e : sk().entities)
            if(e.type == SkType::Line) QVERIFY(e.construction);
    }

    void finishCommitsAsOneUndoStep() {
        cad::Document &doc = m_window->document();
        startSketchOnXY();
        trigger("sketchCircle");
        click(at(0, 0));
        click(at(10, 0));
        QCOMPARE(countType(sk(), SkType::Circle), 1);
        trigger("finishSketch");
        QVERIFY(!mode()->active());
        QVERIFY(m_window->ribbon()->currentTab() == m_window->solidTab());
        QCOMPARE(int(doc.features().size()), 1);
        auto f = std::dynamic_pointer_cast<const cad::SketchFeature>(doc.features()[0]);
        QCOMPARE(countType(f->sketch, SkType::Circle), 1);
        const auto st = doc.displayedState();
        QCOMPARE(int(st->sketches.at(f->id)->profiles.size()), 1);

        // Editing adds one more undo step; undoing it restores the circle only.
        QVERIFY(mode()->editSketch(f->id, false));
        trigger("sketchLine");
        click(at(20, 0));
        click(at(40, 0));
        trigger("finishSketch");
        f = std::dynamic_pointer_cast<const cad::SketchFeature>(doc.features()[0]);
        QCOMPARE(countType(f->sketch, SkType::Line), 1);
        trigger("undo");
        f = std::dynamic_pointer_cast<const cad::SketchFeature>(doc.features()[0]);
        QCOMPARE(countType(f->sketch, SkType::Line), 0);
        QCOMPARE(countType(f->sketch, SkType::Circle), 1);
        trigger("undo");
        QVERIFY(doc.features().empty());
    }

    void sketchUndoIsLocalWhileEditing() {
        cad::Document &doc = m_window->document();
        startSketchOnXY();
        trigger("sketchPoint");
        click(at(5, 5));
        click(at(10, 5));
        QCOMPARE(countType(sk(), SkType::Point), 2);
        trigger("undo");
        QCOMPARE(countType(sk(), SkType::Point), 1);
        QVERIFY(mode()->active());
        QCOMPARE(int(doc.features().size()), 1); // the document is untouched until Finish
    }
};

CADLY_REGISTER_TEST(SketchTests)

#include "tst_sketch.moc"
