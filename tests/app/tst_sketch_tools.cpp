// Sketch Mirror, Circular Pattern and Project, construction geometry from
// the right-click menu, and deleting a sketch from the browser.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "command/CommandPanel.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "sketch/SketchTools.h"
#include "ui/BrowserTree.h"
#include "viewport/Viewport.h"

#include "features/ExtrudeFeature.h"
#include "features/SketchFeature.h"
#include "sketch/SketchText.h"

#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QtTest>

#include <algorithm>

using namespace cadjitsu;
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

} // namespace

class SketchToolTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;

    Viewport *vp() { return m_window->viewport(); }
    SketchMode *mode() { return m_window->sketchMode(); }
    SketchEditor *ed() { return mode()->editor(); }
    const cad::Sketch &sk() { return ed()->sketch(); }
    cad::Document &doc() { return m_window->document(); }

    QPointF at(double x, double y) { return ed()->toScreen({x, y}); }
    void move(QPointF p) { send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton); }
    void click(QPointF p, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        move(p);
        send(vp(), QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, mods);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, mods);
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
    void startSketch(cad::PlaneRef::Kind plane) {
        QVERIFY(mode()->beginNewSketch(cad::PlaneRef::origin(plane), false));
        QVERIFY(mode()->active());
        QVERIFY(waitForFrames(vp(), 1));
    }
    std::vector<int> ofType(SkType t) {
        std::vector<int> out;
        for(const auto &e : sk().entities)
            if(e.type == t) out.push_back(e.id);
        return out;
    }

    SelectionField *field(const char *name) {
        return m_window->commandPanel()->findChild<SelectionField *>(QString::fromLatin1(name));
    }

    void drag(QPointF from, QPointF to) {
        move(from);
        send(vp(), QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for(int i = 1; i <= 5; ++i) send(vp(), QEvent::MouseMove, from + (to - from) * (i / 5.0), Qt::NoButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
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

    void mirrorDialogPicksObjectsThenTheLine() {
        startSketch(cad::PlaneRef::Kind::XY);
        // A U opening onto the Y axis.
        ed()->edit(QStringLiteral("U"), [](cad::Sketch &s) {
            const int p0 = s.addPoint(0, 0), p1 = s.addPoint(20, 0), p2 = s.addPoint(20, 10), p3 = s.addPoint(0, 10);
            s.addLine(p0, p1);
            s.addLine(p1, p2);
            s.addLine(p2, p3);
        }, false);
        QVERIFY(ed()->profiles().empty());
        // Nothing selected: the dialog opens at once, picking Objects.
        trigger("sketchMirror");
        QCOMPARE(mode()->tool(), SketchToolKind::Mirror);
        CommandPanel *panel = m_window->commandPanel();
        QVERIFY(panel->isOpen());
        SelectionField *objects = field("sketchMirrorObjects"), *line = field("sketchMirrorLine");
        QVERIFY(objects && line);
        QVERIFY(objects->active() && !line->active());
        QVERIFY(!panel->okButton()->isEnabled());
        click(at(10, 0));
        click(at(20, 5));
        click(at(10, 10));
        QCOMPARE(objects->count(), 3);
        // The cross clears them; pick them again.
        emit objects->cleared();
        QCOMPARE(objects->count(), 0);
        for(QPointF p : {at(10, 0), at(20, 5), at(10, 10)}) click(p);
        QCOMPARE(objects->count(), 3);
        // The Mirror Line field: hovering the Y axis previews, clicking picks it.
        emit line->activated();
        QVERIFY(line->active() && !objects->active());
        move(at(0, 5));
        QVERIFY(!ed()->previewLines.empty());
        click(at(0, 5));
        QCOMPARE(line->text(), QStringLiteral("Y axis"));
        QVERIFY(!ed()->previewLines.empty());
        QCOMPARE(countType(sk(), SkType::Line), 3); // not yet
        QVERIFY(panel->okButton()->isEnabled());
        panel->okButton()->click();
        QVERIFY(!panel->isOpen());
        QCOMPARE(mode()->tool(), SketchToolKind::Select);
        QCOMPARE(countType(sk(), SkType::Line), 6);
        QCOMPARE(int(ed()->profiles().size()), 1);
        QCOMPARE(QString(ed()->undoLabel()), QStringLiteral("Mirror"));
        int symmetric = 0;
        for(const auto &c : sk().constraints) symmetric += c.type == cad::SkCon::Symmetric;
        QCOMPARE(symmetric, 2);
        QVERIFY(mode()->undo());
        QCOMPARE(countType(sk(), SkType::Line), 3);
    }

    void dialogObjectsCanBeBoxSelected() {
        startSketch(cad::PlaneRef::Kind::XY);
        ed()->edit(QStringLiteral("U"), [](cad::Sketch &s) {
            const int p0 = s.addPoint(0, 0), p1 = s.addPoint(20, 0), p2 = s.addPoint(20, 10), p3 = s.addPoint(0, 10);
            s.addLine(p0, p1);
            s.addLine(p1, p2);
            s.addLine(p2, p3);
        }, false);
        trigger("sketchMirror");
        SelectionField *objects = field("sketchMirrorObjects");
        QVERIFY(objects->active());
        // Left to right, around it all: a window picks the three lines.
        drag(at(-3, -3), at(23, 13));
        QCOMPARE(objects->count(), 3);
        QVERIFY(objects->active()); // still picking objects
        // Right to left: a crossing box picks what it touches.
        emit objects->cleared();
        QCOMPARE(objects->count(), 0);
        drag(at(22, 2), at(18, 8));
        QCOMPARE(objects->count(), 1); // only the vertical line at x = 20 lies across it
        drag(at(25, 12), at(15, -2));
        QCOMPARE(objects->count(), 3); // adds the two touched horizontal lines
        // A window that holds nothing whole adds nothing.
        emit objects->cleared();
        drag(at(5, -3), at(15, 13));
        QCOMPARE(objects->count(), 0);
        // A click still toggles one.
        click(at(10, 0));
        QCOMPARE(objects->count(), 1);
        click(at(10, 0));
        QCOMPARE(objects->count(), 0);
        // Then on with the box-selected ones.
        drag(at(-3, -3), at(23, 13));
        emit field("sketchMirrorLine")->activated();
        click(at(0, 5));
        m_window->commandPanel()->okButton()->click();
        QCOMPARE(countType(sk(), SkType::Line), 6);
    }

    void mirrorWithGeometrySelectedStartsAtTheLineAndCancelLeavesIt() {
        startSketch(cad::PlaneRef::Kind::XY);
        ed()->edit(QStringLiteral("box"), [](cad::Sketch &s) { s.addRectangle({5, 0}, {20, 10}); }, false);
        ed()->selectEntities(ofType(SkType::Line), false);
        trigger("sketchMirror");
        QCOMPARE(field("sketchMirrorObjects")->count(), 4);
        QVERIFY(field("sketchMirrorLine")->active());
        click(at(0, -5)); // the Y axis, below the box
        QCOMPARE(field("sketchMirrorLine")->text(), QStringLiteral("Y axis"));
        m_window->commandPanel()->cancelButton()->click();
        QVERIFY(!m_window->commandPanel()->isOpen());
        QCOMPARE(countType(sk(), SkType::Line), 4);
        QCOMPARE(mode()->tool(), SketchToolKind::Select);
    }

    void circularPatternDialogPreviewsAsTheQuantityIsTyped() {
        startSketch(cad::PlaneRef::Kind::XY);
        ed()->edit(QStringLiteral("hole"), [](cad::Sketch &s) { s.addCircle(Vec2(30, 0), 4); }, false);
        trigger("sketchCircularPattern");
        QCOMPARE(mode()->tool(), SketchToolKind::CircularPattern);
        CommandPanel *panel = m_window->commandPanel();
        QVERIFY(panel->isOpen()); // from the start
        // The hole, box-selected.
        QVERIFY(field("sketchPatternObjects")->active());
        drag(at(24, -6), at(36, 6));
        QCOMPARE(field("sketchPatternObjects")->count(), 1);
        SelectionField *centre = field("sketchPatternCentre");
        emit centre->activated();
        QVERIFY(centre && centre->active());
        ValueField *count = panel->findChild<ValueField *>(QStringLiteral("sketchPatternCount"));
        QVERIFY(count);
        QVERIFY(!panel->okButton()->isEnabled());
        // The centre: the sketch origin.
        click(at(0, 0));
        QCOMPARE(centre->text(), QStringLiteral("Origin"));
        const size_t six = ed()->previewLines.size();
        QVERIFY(six > 0);
        count->setExpression(QStringLiteral("3"));
        QVERIFY(ed()->previewLines.size() < six); // fewer copies shown
        count->setExpression(QStringLiteral("8"));
        QVERIFY(ed()->previewLines.size() > six);
        count->setExpression(QStringLiteral("1"));
        QVERIFY(!panel->message().isEmpty()); // too few
        QVERIFY(!panel->okButton()->isEnabled());
        count->setExpression(QStringLiteral("8"));
        QCOMPARE(countType(sk(), SkType::Circle), 1); // not yet
        panel->okButton()->click();
        QCOMPARE(countType(sk(), SkType::Circle), 8);
        QCOMPARE(int(ed()->profiles().size()), 8);
        QVERIFY(!panel->isOpen());
    }

    void constructionFromTheRightClickMenuIsDottedAndMakesNoProfile() {
        startSketch(cad::PlaneRef::Kind::XY);
        ed()->edit(QStringLiteral("box"), [](cad::Sketch &s) { s.addRectangle({0, 0}, {20, 10}); }, false);
        QCOMPARE(int(ed()->profiles().size()), 1);
        // Right-click the bottom line: the menu offers "Make Construction".
        const QPoint global = vp()->mapToGlobal(at(10, 0).toPoint());
        QString first;
        QTimer::singleShot(150, this, [&] {
            QMenu *m = m_window->sketchEntityMenu();
            if(!m) return;
            first = m->actions().front()->text();
            m->actions().front()->trigger();
            m->close();
        });
        QVERIFY(m_window->showSketchEntityMenu(global));
        QCOMPARE(first, QStringLiteral("Make Construction"));
        QCOMPARE(int(std::count_if(sk().entities.begin(), sk().entities.end(),
                                   [](const cad::SkEntity &e) { return e.construction; })),
                 1);
        QVERIFY(ed()->profiles().empty());
        // X (with it selected) turns it back.
        trigger("sketchConstruction");
        QCOMPARE(int(ed()->profiles().size()), 1);
        // Empty space: no entity menu (the marking menu instead).
        QVERIFY(!m_window->showSketchEntityMenu(vp()->mapToGlobal(at(60, 60).toPoint())));
    }

    void projectALineFromAnotherSketchAndItFollows() {
        // A rectangle on XY, then a sketch on XZ projecting its front edge.
        startSketch(cad::PlaneRef::Kind::XY);
        ed()->edit(QStringLiteral("box"), [](cad::Sketch &s) { s.addRectangle({0, 0}, {20, 10}); }, false);
        const cad::FeatureId base = ed()->featureId();
        mode()->finish();
        QVERIFY(m_window->waitForModel());
        startSketch(cad::PlaneRef::Kind::XZ);
        const cad::FeatureId side = ed()->featureId();
        trigger("sketchProject");
        QCOMPARE(mode()->tool(), SketchToolKind::Project);
        // Click the rectangle's front edge where it shows on the screen.
        const QPointF edge = vp()->camera().project(QVector3D(10, 0, 0));
        move(edge);
        click(edge);
        int projectedLine = 0;
        for(const auto &e : sk().entities)
            if(e.type == SkType::Line && e.isProjected()) projectedLine = e.id;
        QVERIFY2(projectedLine, "the edge was not projected");
        QCOMPARE(sk().find(projectedLine)->projSketch, base);
        // A projected line is a normal line: it can become construction and back.
        ed()->selectEntities({projectedLine}, false);
        trigger("sketchConstruction");
        QVERIFY(sk().find(projectedLine)->construction);
        trigger("sketchConstruction");
        QVERIFY(!sk().find(projectedLine)->construction);
        mode()->finish();
        QVERIFY(m_window->waitForModel());
        // Widen the rectangle: the projected line follows.
        auto wider = std::static_pointer_cast<cad::SketchFeature>(doc().feature(base)->clone());
        for(auto &e : wider->sketch.entities)
            if(e.type == SkType::Point && e.x > 10) e.x = 35;
        doc().replaceFeature(wider);
        const cad::StatePtr st = doc().stateAt(doc().indexOf(side) + 1);
        const cad::SketchResult &r = *st->sketches.at(side);
        const cad::SkEntity &l = *r.sketch.find(projectedLine);
        QCOMPARE(distance(r.sketch.pointPos(l.a), r.sketch.pointPos(l.b)), 35.0);
    }

    void deletingASketchFromTheBrowser() {
        startSketch(cad::PlaneRef::Kind::XY);
        ed()->edit(QStringLiteral("box"), [](cad::Sketch &s) { s.addRectangle({0, 0}, {20, 10}); }, false);
        const cad::FeatureId id = ed()->featureId();
        mode()->finish();
        QVERIFY(m_window->waitForModel());
        const size_t before = doc().features().size();
        QVERIFY(m_window->deleteSketch(id, false));
        QCOMPARE(doc().features().size(), before - 1);
        QVERIFY(!doc().feature(id));
        // One undo step brings it back.
        m_window->undo();
        QVERIFY(doc().feature(id));
        // Not a sketch: nothing happens.
        QVERIFY(!m_window->deleteSketch(9999, false));
    }
};

CADJITSU_REGISTER_TEST(SketchToolTests)

#include "tst_sketch_tools.moc"
