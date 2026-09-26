// Usability fixes from trying the macOS build: readable (light) colours even
// in dark mode, value boxes that replace their value when typed into, numbers
// typed on the canvas going to the command's value (shown in a box on the
// canvas), right drag orbiting (with the mouse settings), and sketch sizes
// edited by double-clicking the geometry.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "command/CanvasValueBox.h"
#include "command/Command.h"
#include "command/CommandPanel.h"
#include "command/ExtrudeCommand.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "sketch/HeadsUpInput.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "ui/BrowserTree.h"
#include "ui/MarkingMenu.h"
#include "ui/SettingsDialog.h"
#include "viewport/Viewport.h"

#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"

#include <QComboBox>
#include <QPushButton>
#include <QSettings>
#include <QtTest>

#include <cmath>

using namespace cadly;

namespace {

void send(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
          Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, mods);
    QCoreApplication::sendEvent(w, &ev);
}

void doubleClick(QWidget *w, QPointF p) {
    send(w, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
    send(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    send(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    send(w, QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton);
    send(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}

} // namespace

class UsabilityTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;

    Viewport *vp() { return m_window->viewport(); }
    cad::Document &doc() { return m_window->document(); }
    ExtrudeCommand *extrude() { return qobject_cast<ExtrudeCommand *>(m_window->commands()->command()); }

    // A 40 x 20 rectangle sketch on the XY plane.
    cad::FeatureId sketch() {
        auto s = std::make_shared<cad::SketchFeature>();
        s->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        s->sketch.addRectangle({0, 0}, {40, 20});
        const cad::FeatureId id = doc().addFeature(s);
        m_window->refresh();
        vp()->setStandardView(StandardView::Top, false);
        vp()->fitAll(false);
        waitForFrames(vp(), 1);
        return id;
    }
    // Extrude of that sketch's profile, from inside the sketch (E).
    bool startExtrude() {
        const cad::FeatureId sid = sketch();
        m_window->editFeature(sid);
        if(!m_window->sketchMode()->active()) return false;
        processEventsFor(500); // the look-at animation
        m_window->startExtrude();
        if(!extrude() || extrude()->profileCount() != 1) return false;
        vp()->setStandardView(StandardView::Home, false);
        return m_window->waitForModel() && waitForFrames(vp(), 2);
    }

private slots:
    void init() {
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1300, 850);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QVERIFY(waitForFrames(vp(), 2));
    }
    void cleanup() { m_window.reset(); }

    void textIsDarkEvenInDarkMode() {
        // The app palette (set by applyLightTheme) has dark text on light panels.
        const QPalette p = QApplication::palette();
        QVERIFY(p.color(QPalette::Text).lightness() < 80);
        QVERIFY(p.color(QPalette::WindowText).lightness() < 80);
        QVERIFY(p.color(QPalette::Base).lightness() > 200);
        QVERIFY(m_window->browser()->palette().color(QPalette::Text).lightness() < 80);
        QVERIFY(m_window->browser()->styleSheet().contains(QStringLiteral("color: #1c2128")));
        QVERIFY(m_window->commandPanel()->styleSheet().contains(QStringLiteral("color: #10161f")));
    }

    void clickingAValueSelectsItSoTypingReplacesIt() {
        QVERIFY(startExtrude());
        ValueField *d = extrude()->distanceField();
        QCOMPARE(d->expression(), QStringLiteral("10 mm"));
        // The value already has the keyboard; a click in the middle of its text
        // (where the cursor would land) still selects it all, then type.
        QTest::mouseClick(d, Qt::LeftButton, Qt::NoModifier, QPoint(d->width() / 3, d->height() / 2));
        QTRY_VERIFY(d->hasSelectedText() && d->selectedText() == d->text());
        QTest::keyClicks(d, QStringLiteral("3"));
        QCOMPARE(d->expression(), QStringLiteral("3"));
        QVERIFY(d->value() && std::fabs(*d->value() - 3.0) < 1e-9); // 3 means 3 mm
        QVERIFY(m_window->waitForModel());
        double v = 0;
        for(const auto &kv : m_window->modelView()->state()->bodies) v += cad::volumeOf(kv.second->shape.shape());
        QVERIFY2(std::fabs(v - 40 * 20 * 3) < 1e-6, qPrintable(QString::number(v)));
    }

    void numbersTypedOnTheCanvasGoToTheValue() {
        QVERIFY(startExtrude());
        CanvasValueBox *box = m_window->commands()->canvasBox();
        QTRY_VERIFY(box->isVisible());
        QCOMPARE(box->text(), QStringLiteral("10 mm"));
        // Beside the arrow's head.
        const QPointF head = extrude()->arrow().headOnScreen();
        QVERIFY(QLineF(head, QPointF(box->geometry().left(), box->geometry().center().y())).length() < 40);
        vp()->setFocus();
        QTest::keyClick(vp(), Qt::Key_2, Qt::NoModifier);
        QCOMPARE(m_window->focusWidget(), static_cast<QWidget *>(box));
        QTest::keyClicks(box, QStringLiteral("5"));
        QCOMPARE(extrude()->distanceField()->expression(), QStringLiteral("25"));
        // And back: the panel's value shows in the canvas box.
        extrude()->distanceField()->setFocus();
        extrude()->distanceField()->selectAll();
        QTest::keyClicks(extrude()->distanceField(), QStringLiteral("7 mm"));
        QCOMPARE(box->text(), QStringLiteral("7 mm"));
        // Enter in the canvas box commits.
        box->setFocus();
        QTest::keyClick(box, Qt::Key_Return);
        QVERIFY(!m_window->commands()->active());
        QVERIFY(m_window->waitForModel());
        double v = 0;
        for(const auto &kv : m_window->modelView()->state()->bodies) v += cad::volumeOf(kv.second->shape.shape());
        QVERIFY(std::fabs(v - 40 * 20 * 7) < 1e-6);
        QVERIFY(!box->isVisible());
    }

    void rightDragOrbitsAndRightClickOpensTheMarkingMenu() {
        sketch();
        vp()->setStandardView(StandardView::Home, false);
        const QQuaternion before = vp()->camera().rotation;
        const QPointF c(vp()->width() / 2.0, vp()->height() / 2.0);
        send(vp(), QEvent::MouseButtonPress, c, Qt::RightButton, Qt::RightButton);
        for(int i = 1; i <= 10; ++i)
            send(vp(), QEvent::MouseMove, c + QPointF(8 * i, 3 * i), Qt::NoButton, Qt::RightButton);
        send(vp(), QEvent::MouseButtonRelease, c + QPointF(80, 30), Qt::RightButton, Qt::NoButton);
        QVERIFY(!qFuzzyCompare(before, vp()->camera().rotation));
        QVERIFY(!m_window->markingMenu()->isOpen());
        // Without moving it is the marking menu.
        send(vp(), QEvent::MouseButtonPress, c, Qt::RightButton, Qt::RightButton);
        send(vp(), QEvent::MouseButtonRelease, c, Qt::RightButton, Qt::NoButton);
        QTRY_VERIFY(m_window->markingMenu()->isOpen());
        m_window->markingMenu()->close();
        // Shift + middle drag still orbits (Fusion's binding) and middle drag pans.
        const QQuaternion r1 = vp()->camera().rotation;
        send(vp(), QEvent::MouseButtonPress, c, Qt::MiddleButton, Qt::MiddleButton, Qt::ShiftModifier);
        send(vp(), QEvent::MouseMove, c + QPointF(60, 0), Qt::NoButton, Qt::MiddleButton, Qt::ShiftModifier);
        send(vp(), QEvent::MouseButtonRelease, c + QPointF(60, 0), Qt::MiddleButton, Qt::NoButton, Qt::ShiftModifier);
        QVERIFY(!qFuzzyCompare(r1, vp()->camera().rotation));
    }

    void settingsRebindTheMouse() {
        QSettings().remove(QStringLiteral("mouse"));
        QCOMPARE(MouseBindings::load().matchingPreset(), MouseBindings::Preset::Cadly);
        SettingsDialog *dlg = m_window->openSettings();
        QVERIFY(dlg);
        QCOMPARE(dlg->presetBox()->currentIndex(), int(MouseBindings::Preset::Cadly));
        // SolidWorks-style: middle drag orbits.
        dlg->presetBox()->setCurrentIndex(int(MouseBindings::Preset::SolidWorks));
        emit dlg->presetBox()->activated(int(MouseBindings::Preset::SolidWorks));
        QCOMPARE(dlg->orbitBox()->currentIndex(), int(MouseBindings::Drag::Middle));
        dlg->accept();
        QCOMPARE(vp()->mouseBindings().orbit, MouseBindings::Drag::Middle);
        QCOMPARE(MouseBindings::load().matchingPreset(), MouseBindings::Preset::SolidWorks);
        const QQuaternion before = vp()->camera().rotation;
        const QPointF c(vp()->width() / 2.0, vp()->height() / 2.0);
        send(vp(), QEvent::MouseButtonPress, c, Qt::MiddleButton, Qt::MiddleButton);
        send(vp(), QEvent::MouseMove, c + QPointF(60, 20), Qt::NoButton, Qt::MiddleButton);
        send(vp(), QEvent::MouseButtonRelease, c + QPointF(60, 20), Qt::MiddleButton, Qt::NoButton);
        QVERIFY(!qFuzzyCompare(before, vp()->camera().rotation));
        QSettings().remove(QStringLiteral("mouse"));
    }

    void doubleClickingALineTypesItsLength() {
        const cad::FeatureId sid = sketch();
        m_window->editFeature(sid);
        SketchMode *mode = m_window->sketchMode();
        QVERIFY(mode->active());
        processEventsFor(500);
        SketchEditor *ed = mode->editor();
        doubleClick(vp(), ed->toScreen({20, 0})); // the bottom edge, 40 long
        QVERIFY(mode->dimensionEditor());
        mode->dimensionEditor()->selectAll();
        QTest::keyClicks(mode->dimensionEditor(), QStringLiteral("30"));
        QTest::keyClick(mode->dimensionEditor(), Qt::Key_Return);
        // The sketch re-solves at once: the bottom edge is 30 long.
        double longest = 0;
        for(const auto &e : ed->sketch().entities)
            if(e.type == cad::SkType::Line && std::fabs(ed->sketch().pointPos(e.a).y) < 1e-6 &&
               std::fabs(ed->sketch().pointPos(e.b).y) < 1e-6)
                longest = (ed->sketch().pointPos(e.b) - ed->sketch().pointPos(e.a)).length();
        QVERIFY2(std::fabs(longest - 30.0) < 1e-6, qPrintable(QString::number(longest)));
        // Double-clicking it again edits the same dimension.
        int dims = 0;
        for(const auto &c : ed->sketch().constraints) dims += cad::isDimension(c.type);
        doubleClick(vp(), ed->toScreen({15, 0}));
        QVERIFY(mode->dimensionEditor());
        QCOMPARE(mode->dimensionEditor()->text(), QStringLiteral("30"));
        QTest::keyClick(mode->dimensionEditor(), Qt::Key_Escape);
        int dims2 = 0;
        for(const auto &c : ed->sketch().constraints) dims2 += cad::isDimension(c.type);
        QCOMPARE(dims2, dims);
    }

    void doubleClickingASketchInTheModelEditsIt() {
        const cad::FeatureId sid = sketch();
        QVERIFY(!m_window->sketchMode()->active());
        const QPointF px = vp()->camera().project(QVector3D(20, 10, 0));
        doubleClick(vp(), px); // inside its profile
        QTRY_VERIFY(m_window->sketchMode()->active());
        QCOMPARE(m_window->sketchMode()->editor()->featureId(), sid);
    }
};

CADLY_REGISTER_TEST(UsabilityTests)

#include "tst_usability.moc"
