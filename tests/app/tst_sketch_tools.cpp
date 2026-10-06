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

    void mirrorAHalfProfileAboutTheYAxis() {
        startSketch(cad::PlaneRef::Kind::XY);
        // A U opening onto the Y axis.
        ed()->edit(QStringLiteral("U"), [](cad::Sketch &s) {
            const int p0 = s.addPoint(0, 0), p1 = s.addPoint(20, 0), p2 = s.addPoint(20, 10), p3 = s.addPoint(0, 10);
            s.addLine(p0, p1);
            s.addLine(p1, p2);
            s.addLine(p2, p3);
        }, false);
        QVERIFY(ed()->profiles().empty());
        ed()->selectEntities(ofType(SkType::Line), false);
        trigger("sketchMirror");
        QCOMPARE(mode()->tool(), SketchToolKind::Mirror);
        // Hovering the Y axis previews the mirror image; clicking it mirrors.
        move(at(0, 5));
        QVERIFY(!ed()->previewLines.empty());
        click(at(0, 5));
        QCOMPARE(countType(sk(), SkType::Line), 6);
        QCOMPARE(int(ed()->profiles().size()), 1);
        QCOMPARE(QString(ed()->undoLabel()), QStringLiteral("Mirror"));
        int symmetric = 0;
        for(const auto &c : sk().constraints) symmetric += c.type == cad::SkCon::Symmetric;
        QCOMPARE(symmetric, 2);
        QVERIFY(mode()->undo());
        QCOMPARE(countType(sk(), SkType::Line), 3);
    }

    void circularPatternPreviewsAsTheCountIsTyped() {
        startSketch(cad::PlaneRef::Kind::XY);
        ed()->edit(QStringLiteral("hole"), [](cad::Sketch &s) { s.addCircle(Vec2(30, 0), 4); }, false);
        ed()->selectEntities(ofType(SkType::Circle), false);
        trigger("sketchCircularPattern");
        QCOMPARE(mode()->tool(), SketchToolKind::CircularPattern);
        // The centre: the sketch origin.
        click(at(0, 0));
        CommandPanel *panel = m_window->commandPanel();
        QVERIFY(panel->isOpen());
        ValueField *count = panel->findChild<ValueField *>(QStringLiteral("sketchPatternCount"));
        QVERIFY(count);
        const size_t six = ed()->previewLines.size();
        QVERIFY(six > 0);
        count->setExpression(QStringLiteral("3"));
        QVERIFY(ed()->previewLines.size() < six); // fewer copies shown
        count->setExpression(QStringLiteral("8"));
        QVERIFY(ed()->previewLines.size() > six);
        count->setExpression(QStringLiteral("1"));
        QVERIFY(!panel->message().isEmpty()); // too few
        count->setExpression(QStringLiteral("8"));
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
