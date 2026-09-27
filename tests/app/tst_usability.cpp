// Usability fixes from trying the macOS build: readable (light) colours even
// in dark mode, value boxes that replace their value when typed into, numbers
// typed on the canvas going to the command's value (shown in a box on the
// canvas), right drag orbiting (with the mouse settings), and sketch sizes
// edited by double-clicking the geometry.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "command/CanvasValueBox.h"
#include "command/CombineCommand.h"
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
#include "ui/Units.h"
#include "viewport/Viewport.h"

#include "features/ExtrudeFeature.h"
#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"

#include <QComboBox>
#include <QMenu>
#include <QToolButton>
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
        QCOMPARE(d->expression(), QString()); // a new extrude waits for its distance
        d->setExpression(QStringLiteral("10 mm")); // as when editing an extrude
        QCOMPARE(d->text(), QStringLiteral("10")); // just the number: the unit is in the drop-down
        QCOMPARE(d->expression(), QStringLiteral("10 mm"));
        // The value already has the keyboard; a click in the middle of its text
        // (where the cursor would land) still selects it all, then type.
        QTest::mouseClick(d, Qt::LeftButton, Qt::NoModifier, QPoint(d->width() / 3, d->height() / 2));
        QTRY_VERIFY(d->hasSelectedText() && d->selectedText() == d->text());
        QTest::keyClicks(d, QStringLiteral("3"));
        QCOMPARE(d->expression(), QStringLiteral("3 mm"));
        QVERIFY(d->value() && std::fabs(*d->value() - 3.0) < 1e-9); // 3 means 3 mm
        QVERIFY(m_window->waitForModel());
        double v = 0;
        for(const auto &kv : m_window->modelView()->state()->bodies) v += cad::volumeOf(kv.second->shape.shape());
        QVERIFY2(std::fabs(v - 40 * 20 * 3) < 1e-6, qPrintable(QString::number(v)));
    }

    // Value boxes hold just the number; its unit is a drop-down beside it, and
    // a number typed in another unit is converted to the default one.
    void valueBoxesShowNumbersWithAUnitDropDown() {
        QVERIFY(startExtrude());
        ValueField *d = extrude()->distanceField();
        auto *suffix = d->findChild<UnitSuffix *>();
        QVERIFY(suffix && suffix->isVisible());
        QCOMPARE(suffix->unit(), QStringLiteral("mm"));
        QVERIFY(suffix->geometry().left() >= d->width() - suffix->width() - 4); // inside the box's right end
        QVERIFY(d->textMargins().right() >= suffix->width());                  // text kept clear of it
        // 1 inch, typed as "1" with "in" picked.
        QTest::keyClicks(d, QStringLiteral("1"));
        suffix->menu()->actions().at(3)->trigger(); // mm, cm, m, in, ft
        QCOMPARE(d->unit(), QStringLiteral("in"));
        QCOMPARE(d->text(), QStringLiteral("1"));
        QCOMPARE(d->expression(), QStringLiteral("25.4 mm"));
        QVERIFY(d->value() && std::fabs(*d->value() - 25.4) < 1e-9);
        // The canvas box mirrors number and unit.
        CanvasValueBox *box = m_window->commands()->canvasBox();
        QTRY_VERIFY(box->isVisible());
        QCOMPARE(box->text(), QStringLiteral("1"));
        QCOMPARE(box->findChild<UnitSuffix *>()->unit(), QStringLiteral("in"));
        // Committed, it is stored (and shown again) in the default unit.
        m_window->commandPanel()->okButton()->click();
        QVERIFY(m_window->waitForModel());
        const auto e = std::dynamic_pointer_cast<const cad::ExtrudeFeature>(doc().features().back());
        QVERIFY(e);
        QCOMPARE(QString::fromStdString(e->distance.expr), QStringLiteral("25.4 mm"));
        m_window->editFeature(e->id);
        QVERIFY(extrude());
        QCOMPARE(extrude()->distanceField()->text(), QStringLiteral("25.4"));
        QCOMPARE(extrude()->distanceField()->unit(), QStringLiteral("mm"));
        // Formulas are taken as typed.
        extrude()->distanceField()->enterExpression(QStringLiteral("1 in + 2"));
        QCOMPARE(extrude()->distanceField()->expression(), QStringLiteral("1 in + 2"));
        QVERIFY(std::fabs(*extrude()->distanceField()->value() - 27.4) < 1e-9);
        m_window->commandPanel()->cancelButton()->click();
    }

    void theDefaultLengthUnitIsASetting() {
        units::setDefaultLengthUnit(QStringLiteral("cm"));
        struct Restore {
            ~Restore() { units::setDefaultLengthUnit(QStringLiteral("mm")); }
        } restore;
        QCOMPARE(units::displayText(QStringLiteral("25 mm"), cad::ValueKind::Length), QStringLiteral("2.5"));
        QCOMPARE(units::toExpression(QStringLiteral("3"), QStringLiteral("cm"), cad::ValueKind::Length),
                 QStringLiteral("3 cm"));
        QCOMPARE(units::toExpression(QStringLiteral("1"), QStringLiteral("in"), cad::ValueKind::Length),
                 QStringLiteral("2.54 cm"));
        QCOMPARE(units::displayText(QStringLiteral("30 deg"), cad::ValueKind::Angle), QStringLiteral("30"));
        QCOMPARE(units::displayText(QStringLiteral("d1 * 2"), cad::ValueKind::Length), QStringLiteral("d1 * 2"));
        // The sketch's heads-up boxes show live values in it, number only.
        QCOMPARE(HeadsUpInput::formatLive(12.5, cad::ValueKind::Length), QStringLiteral("1.25"));
        // The Settings dialog offers it.
        SettingsDialog *dlg = m_window->openSettings();
        QCOMPARE(dlg->lengthUnit(), QStringLiteral("cm"));
        dlg->lengthUnitBox()->setCurrentIndex(3);
        dlg->accept();
        QCOMPARE(units::defaultUnit(cad::ValueKind::Length), QStringLiteral("in"));
    }

    // Sketch: a Select button puts down the drawing tool.
    void theSketchSelectButtonLeavesADrawingTool() {
        sketch();
        m_window->editFeature(doc().features().front()->id);
        SketchMode *mode = m_window->sketchMode();
        QVERIFY(mode->active());
        QAction *select = m_window->action(QStringLiteral("sketchSelect"));
        QVERIFY(select && select->isCheckable());
        QVERIFY(select->isChecked());
        m_window->action(QStringLiteral("sketchLine"))->trigger();
        QCOMPARE(mode->tool(), SketchToolKind::Line);
        QVERIFY(!select->isChecked());
        select->trigger();
        QCOMPARE(mode->tool(), SketchToolKind::Select);
        QVERIFY(select->isChecked());
        QVERIFY(!m_window->action(QStringLiteral("sketchLine"))->isChecked());
    }

    // The navigation bar's menu buttons draw their own chevron beside the icon.
    void navigationBarMenusHaveTheirOwnChevron() {
        for(const char *name : {"navDisplay", "navCamera"}) {
            auto *b = vp()->findChild<QToolButton *>(QString::fromLatin1(name));
            QVERIFY2(b && b->menu(), name);
            QVERIFY(b->width() >= b->iconSize().width() + 16); // room for the chevron beside the icon
        }
        auto *grid = vp()->findChild<QToolButton *>(QStringLiteral("navGrid"));
        QVERIFY(grid && !grid->menu() && grid->width() < 36);
    }

    void numbersTypedOnTheCanvasGoToTheValue() {
        QVERIFY(startExtrude());
        CanvasValueBox *box = m_window->commands()->canvasBox();
        QTRY_VERIFY(box->isVisible());
        QCOMPARE(box->text(), QString());
        // Beside the arrow's head.
        const QPointF head = extrude()->arrow().headOnScreen();
        QVERIFY(QLineF(head, QPointF(box->geometry().left(), box->geometry().center().y())).length() < 40);
        vp()->setFocus();
        QTest::keyClick(vp(), Qt::Key_2, Qt::NoModifier);
        QCOMPARE(m_window->focusWidget(), static_cast<QWidget *>(box));
        QTest::keyClicks(box, QStringLiteral("5"));
        QCOMPARE(extrude()->distanceField()->expression(), QStringLiteral("25 mm"));
        // And back: the panel's value shows in the canvas box.
        extrude()->distanceField()->setFocus();
        extrude()->distanceField()->selectAll();
        QTest::keyClicks(extrude()->distanceField(), QStringLiteral("7"));
        QCOMPARE(box->text(), QStringLiteral("7"));
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

    // Bug report: a sketch drawn over a body and extruded cut into that body.
    // Extrudes make new bodies; Combine joins or subtracts them afterwards.
    void extrudeMakesANewBodyByDefault() {
        QVERIFY(!ExtrudeCommand::autoOperation());
        auto base = std::make_shared<cad::SketchFeature>();
        base->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        base->sketch.addRectangle({0, 0}, {40, 20});
        const cad::FeatureId bid = doc().addFeature(base);
        auto boxExtrude = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &p : doc().stateAt(doc().marker())->sketches.at(bid)->profiles)
            boxExtrude->profiles.push_back({bid, p.key, p.sample});
        boxExtrude->distance = doc().makeSlot("10 mm");
        doc().addFeature(boxExtrude);
        // A sketch overlapping the box, extruded through it from the dialog.
        auto over = std::make_shared<cad::SketchFeature>();
        over->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        over->sketch.addRectangle({30, 5}, {50, 15});
        const cad::FeatureId oid = doc().addFeature(over);
        m_window->refresh();
        m_window->editFeature(oid);
        QVERIFY(m_window->sketchMode()->active());
        processEventsFor(500);
        m_window->startExtrude();
        QVERIFY(extrude() && extrude()->profileCount() == 1);
        QCOMPARE(extrude()->operation(), cad::BodyOperation::NewBody);
        QTest::keyClicks(extrude()->distanceField(), QStringLiteral("10"));
        QVERIFY(m_window->waitForModel());
        QCOMPARE(extrude()->operation(), cad::BodyOperation::NewBody); // still, although it runs through the box
        m_window->commandPanel()->okButton()->click();
        QVERIFY(m_window->waitForModel());
        auto volumes = [&] {
            std::vector<double> v;
            for(const cad::Body *b : m_window->modelView()->state()->orderedBodies()) v.push_back(cad::volumeOf(b->shape.shape()));
            return v;
        };
        std::vector<double> v = volumes();
        QCOMPARE(int(v.size()), 2);
        QVERIFY(std::fabs(v[0] - 8000.0) < 1e-6); // the box is untouched
        QVERIFY(std::fabs(v[1] - 2000.0) < 1e-6);

        // Combine > Cut subtracts the new body from the box.
        vp()->setStandardView(StandardView::Home, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 1));
        m_window->action(QStringLiteral("combine"))->trigger();
        auto *combine = qobject_cast<CombineCommand *>(m_window->commands()->command());
        QVERIFY(combine);
        auto click = [&](QVector3D w) {
            const QPointF p = vp()->camera().project(w);
            send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
            send(vp(), QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
            send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        };
        click({10, 10, 10});  // the box's top: the target
        click({45, 10, 10});  // the new body's top, outside the box: the tool
        QVERIFY(combine->hasTarget() && combine->toolCount() == 1);
        combine->operationBox()->setCurrentIndex(1); // Cut
        QVERIFY(m_window->waitForModel());
        m_window->commandPanel()->okButton()->click();
        QVERIFY(m_window->waitForModel());
        v = volumes();
        QCOMPARE(int(v.size()), 1);
        QVERIFY2(std::fabs(v[0] - (8000.0 - 10 * 10 * 10)) < 1e-6, qPrintable(QString::number(v[0])));
    }

    // Bug report: going back into the sketch a body was extruded from and
    // drawing across it cut the body (its region was split by the new lines).
    void drawingInAUsedSketchLeavesTheBodyAlone() {
        QVERIFY(startExtrude()); // the 40 x 20 rectangle
        const cad::FeatureId sid = doc().features().front()->id;
        QTest::keyClicks(extrude()->distanceField(), QStringLiteral("10"));
        QCOMPARE(extrude()->distanceField()->expression(), QStringLiteral("10 mm"));
        m_window->commandPanel()->okButton()->click();
        QVERIFY2(!m_window->commands()->active(), qPrintable(m_window->commandPanel()->message()));
        QVERIFY(m_window->waitForModel());
        auto volume = [&] {
            double v = 0;
            for(const auto &kv : m_window->modelView()->state()->bodies) v += cad::volumeOf(kv.second->shape.shape());
            return v;
        };
        QVERIFY(std::fabs(volume() - 8000.0) < 1e-6);
        // Edit Sketch, and draw a line right across the rectangle.
        m_window->editFeature(sid);
        SketchMode *mode = m_window->sketchMode();
        QVERIFY(mode->active());
        processEventsFor(500);
        SketchEditor *ed = mode->editor();
        m_window->action(QStringLiteral("sketchLine"))->trigger();
        auto click = [&](QPointF p) {
            send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
            send(vp(), QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
            send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        };
        click(ed->toScreen({12, -6}));
        click(ed->toScreen({27, 26}));
        QTest::keyClick(vp(), Qt::Key_Escape);
        QVERIFY(ed->profiles().size() >= 2); // the line split the rectangle
        m_window->action(QStringLiteral("finishSketch"))->trigger();
        QVERIFY(!mode->active());
        QVERIFY(m_window->waitForModel());
        QString statuses;
        for(const auto &f : doc().features())
            statuses += QString::fromStdString(f->name + ": " + doc().statusOf(f->id).message + " / " + f->toJson().dump()) + QStringLiteral("\n");
        QVERIFY2(int(m_window->modelView()->state()->bodies.size()) == 1, qPrintable(statuses));
        QVERIFY2(std::fabs(volume() - 8000.0) < 1e-6, qPrintable(QString::number(volume())));
        for(const auto &f : doc().features()) QVERIFY(doc().statusOf(f->id).isOk());
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
