// Solid modelling UI: extruding sketch profiles and faces through the command
// panel with live preview, the manipulator arrow, automatic join / cut, Edit
// Feature, the timeline, the browser and the marking menu.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "features/ConstructionPlaneFeature.h"
#include "command/Command.h"
#include "command/CommandPanel.h"
#include "command/ExtrudeCommand.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "sketch/SketchMode.h"
#include "ui/BrowserTree.h"
#include "ui/MarkingMenu.h"
#include "ui/TimelineWidget.h"
#include "viewport/Viewport.h"

#include "features/ExtrudeFeature.h"
#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace cadly;

namespace {

// A whole double click. (QTest::mouseDClick sends only the double-click event
// in Qt 6; item views need the press before it.)
void doubleClick(QWidget *w, QPoint pos) {
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, pos);
}

void send(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
          Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, mods);
    QCoreApplication::sendEvent(w, &ev);
}

double volume(const cad::StatePtr &s) {
    double v = 0;
    for(const auto &kv : s->bodies) v += cad::volumeOf(kv.second->shape.shape());
    return v;
}

std::shared_ptr<cad::SketchFeature> rectangle(cad::PlaneRef plane, cad::Vec2 a, cad::Vec2 b) {
    auto s = std::make_shared<cad::SketchFeature>();
    s->plane = plane;
    s->sketch.addRectangle(a, b);
    return s;
}

} // namespace

class ModelingTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;
    bool m_active = false;

    Viewport *vp() { return m_window->viewport(); }
    cad::Document &doc() { return m_window->document(); }
    ModelView *view() { return m_window->modelView(); }
    CommandPanel *panel() { return m_window->commandPanel(); }
    template <class T> T *field(const char *name) { return panel()->findChild<T *>(QString::fromLatin1(name)); }
    ExtrudeCommand *extrude() { return qobject_cast<ExtrudeCommand *>(m_window->commands()->command()); }

    QPointF at(double x, double y, double z) { return vp()->camera().project(QVector3D(float(x), float(y), float(z))); }
    void move(QPointF p) { send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton); }
    void click(QPointF p, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        move(p);
        send(vp(), QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, mods);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, mods);
    }
    void settle() {
        QVERIFY(m_window->waitForModel());
        QVERIFY(waitForFrames(vp(), 1));
    }
    // Presses a shortcut on the canvas. Some CI desktops cannot activate the
    // window, and shortcuts need an active one: the action is triggered then.
    void shortcut(Qt::Key key, const char *action) {
        if(m_active) {
            QTest::keyClick(vp(), key);
            return;
        }
        qWarning("window not active: triggering %s instead of pressing its shortcut", action);
        QCOMPARE(m_window->action(QString::fromLatin1(action))->shortcut(), QKeySequence(key));
        m_window->action(QString::fromLatin1(action))->trigger();
    }
    void typeInto(ValueField *f, const QString &text) {
        f->setFocus();
        f->selectAll();
        QTest::keyClicks(f, text);
    }
    // A 40 x 20 rectangle sketch on XY, shown from the top.
    cad::FeatureId baseSketch() {
        const cad::FeatureId id = doc().addFeature(rectangle(cad::PlaneRef::origin(cad::PlaneRef::Kind::XY), {0, 0}, {40, 20}));
        m_window->refresh();
        vp()->setStandardView(StandardView::Top, false);
        vp()->fitAll(false);
        return id;
    }
    // Extrudes the base sketch's profile through the UI.
    void extrudeBase(const QString &distance) {
        click(at(20, 10, 0));
        QCOMPARE(view()->selection().count(SelectionItem::Kind::Profile), size_t(1));
        m_window->action(QStringLiteral("extrude"))->trigger();
        QVERIFY(m_window->commands()->active());
        typeInto(field<ValueField>("extrudeDistance"), distance);
        settle();
        panel()->okButton()->click();
        settle();
    }

private slots:
    void init() {
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1300, 850);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        // Shortcuts (E...) need the window to be active, as it is for a user.
        m_window->activateWindow();
        m_active = QTest::qWaitForWindowActive(m_window.get(), 3000);
        vp()->setFocus();
        QVERIFY(waitForFrames(vp(), 2));
    }

    void cleanup() {
        m_window.reset();
        ExtrudeCommand::setAutoOperation(false);
    }

    void extrudeAProfileWithLivePreview() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        // Clicking inside the sketch picks its profile (it wins over the grid / nothing).
        click(at(20, 10, 0));
        QCOMPARE(view()->selection().count(SelectionItem::Kind::Profile), size_t(1));
        QVERIFY(m_window->selectionStatsLabel()->text().contains(QStringLiteral("800.00")));
        shortcut(Qt::Key_E, "extrude");
        QVERIFY(m_window->commands()->active());
        QVERIFY(panel()->isVisible());
        auto *profiles = field<SelectionField>("extrudeProfiles");
        QVERIFY(profiles);
        QCOMPARE(profiles->count(), 1);
        settle();
        // The default 10 mm preview is on screen, but not in the document yet.
        QVERIFY(view()->evaluation()->preview);
        QVERIFY(std::fabs(volume(view()->state()) - 8000.0) < 1e-6);
        QCOMPARE(int(doc().features().size()), 1);
        // Typing a distance updates the preview.
        typeInto(field<ValueField>("extrudeDistance"), QStringLiteral("25"));
        settle();
        QVERIFY(std::fabs(volume(view()->state()) - 20000.0) < 1e-6);
        QCOMPARE(extrude()->operation(), cad::BodyOperation::NewBody);
        // OK commits one feature and one undo step.
        panel()->okButton()->click();
        QVERIFY(!m_window->commands()->active());
        settle();
        QCOMPARE(int(doc().features().size()), 2);
        QVERIFY(!view()->evaluation()->preview);
        QVERIFY(std::fabs(volume(view()->state()) - 20000.0) < 1e-6);
        const auto e = std::dynamic_pointer_cast<const cad::ExtrudeFeature>(doc().features()[1]);
        QVERIFY(e);
        QCOMPARE(e->distance.expr, std::string("25"));
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Create Extrude1"));
        // The used sketch is hidden, as in Fusion.
        QVERIFY(!view()->sketchShown(doc().features()[0]->id));
    }

    void cancellingLeavesTheDocumentAlone() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        click(at(20, 10, 0));
        m_window->action(QStringLiteral("extrude"))->trigger();
        settle();
        QVERIFY(volume(view()->state()) > 0);
        panel()->cancelButton()->click();
        settle();
        QCOMPARE(int(doc().features().size()), 1);
        QCOMPARE(volume(view()->state()), 0.0);
        QVERIFY(!panel()->isVisible());
    }

    // With Settings > "Choose Join / Cut automatically" on (off by default).
    void automaticJoinAndCut() {
        ExtrudeCommand::setAutoOperation(true);
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        extrudeBase(QStringLiteral("10"));
        // A circle on the top face.
        const cad::Body *box = view()->state()->bodies.begin()->second.get();
        int top = 0;
        for(int i = 1; i <= box->shape.faceCount(); ++i) {
            gp_Pln p;
            if(cad::planeOfFace(box->shape.face(i), p) && p.Axis().Direction().IsEqual(gp_Dir(0, 0, 1), 1e-9)) top = i;
        }
        QVERIFY(top);
        auto circle = std::make_shared<cad::SketchFeature>();
        circle->plane = cad::PlaneRef::onFace(cad::makeTopoRef(*box, cad::TopoKind::Face, top));
        circle->sketch.addCircle(cad::Vec2{20, 10}, 5);
        doc().addFeature(circle);
        m_window->refresh();
        QVERIFY(waitForFrames(vp(), 1));
        click(at(20, 10, 10));
        QCOMPARE(view()->selection().count(SelectionItem::Kind::Profile), size_t(1));
        m_window->action(QStringLiteral("extrude"))->trigger();
        settle();
        // Outwards from the face: Join.
        QCOMPARE(extrude()->operation(), cad::BodyOperation::Join);
        QVERIFY(std::fabs(volume(view()->state()) - (8000.0 + M_PI * 25 * 10)) < 1e-3);
        // Into the body: Cut.
        typeInto(field<ValueField>("extrudeDistance"), QStringLiteral("-4"));
        settle();
        QCOMPARE(extrude()->operation(), cad::BodyOperation::Cut);
        QVERIFY(std::fabs(volume(view()->state()) - (8000.0 - M_PI * 25 * 4)) < 1e-3);
        // Through all, flipped.
        auto *extent = field<QComboBox>("extrudeExtent");
        extent->setCurrentIndex(2);
        field<QCheckBox>("extrudeFlip")->setChecked(true);
        settle();
        QCOMPARE(extrude()->operation(), cad::BodyOperation::Cut);
        QVERIFY(std::fabs(volume(view()->state()) - (8000.0 - M_PI * 25 * 10)) < 1e-3);
        panel()->okButton()->click();
        settle();
        QVERIFY(std::fabs(volume(view()->state()) - (8000.0 - M_PI * 25 * 10)) < 1e-3);
    }

    void dragTheArrow() {
        baseSketch();
        vp()->setStandardView(StandardView::Home, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 1));
        click(at(20, 10, 0));
        m_window->action(QStringLiteral("extrude"))->trigger();
        settle();
        DistanceManipulator &arrow = extrude()->arrow();
        QVERIFY(arrow.visible());
        const QPointF head = arrow.headOnScreen();
        // Drag the head up the screen: the distance grows.
        const QVector3D target = arrow.origin() + arrow.direction() * 30.0f;
        const QPointF to = at(target.x(), target.y(), target.z());
        send(vp(), QEvent::MouseMove, head, Qt::NoButton, Qt::NoButton);
        send(vp(), QEvent::MouseButtonPress, head, Qt::LeftButton, Qt::LeftButton);
        for(int i = 1; i <= 10; ++i) send(vp(), QEvent::MouseMove, head + (to - head) * (i / 10.0), Qt::NoButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        QVERIFY(!arrow.dragging());
        const auto d = field<ValueField>("extrudeDistance")->value();
        QVERIFY(d.has_value());
        QVERIFY2(std::fabs(*d - 30.0) < 0.02, qPrintable(QString::number(*d)));
        settle();
        QVERIFY(std::fabs(volume(view()->state()) - 800.0 * *d) < 1e-3);
        QCOMPARE(extrude()->profileCount(), 1); // the drag did not deselect
        panel()->cancelButton()->click();
    }

    void toObjectPicksAFace() {
        // A slab above the sketch plane to extrude up to.
        auto plane = std::make_shared<cad::SketchFeature>();
        const cad::FeatureId slabSketch = doc().addFeature(rectangle(cad::PlaneRef::origin(cad::PlaneRef::Kind::XZ), {-10, 30}, {60, 35}));
        auto slab = std::make_shared<cad::ExtrudeFeature>();
        const auto st0 = doc().stateAt(1);
        for(const auto &p : st0->sketches.at(slabSketch)->profiles) slab->profiles.push_back({slabSketch, p.key, p.sample});
        slab->distance = doc().makeSlot("40 mm");
        slab->direction = cad::ExtrudeDirection::Symmetric;
        doc().addFeature(slab);
        baseSketch();
        vp()->setStandardView(StandardView::Front, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 1));
        // Seen from the front the rectangle is edge-on: pick its profile from below instead.
        vp()->setStandardView(StandardView::Bottom, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 1));
        click(at(20, 10, 0));
        QCOMPARE(view()->selection().count(SelectionItem::Kind::Profile), size_t(1));
        m_window->action(QStringLiteral("extrude"))->trigger();
        field<QComboBox>("extrudeExtent")->setCurrentIndex(1); // To Object
        QVERIFY(field<SelectionField>("extrudeObject")->active());
        settle();
        QVERIFY(!panel()->message().isEmpty()); // asks for the face
        // The slab's underside (z = 30), seen from below.
        click(at(20, 10, 30));
        QCOMPARE(field<SelectionField>("extrudeObject")->count(), 1);
        settle();
        const double slabVolume = 70 * 5 * 80;
        QVERIFY2(std::fabs(volume(view()->state()) - (slabVolume + 800 * 30)) < 1e-3,
                 qPrintable(QString::number(volume(view()->state()))));
        panel()->okButton()->click();
        settle();
        QCOMPARE(doc().statusOf(doc().features().back()->id).severity, cad::Severity::Ok);
    }

    void editFeatureReopensTheSameDialog() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        extrudeBase(QStringLiteral("15"));
        const cad::FeatureId eid = doc().features()[1]->id;
        // Double-click the extrude in the timeline.
        TimelineWidget *tl = m_window->timeline();
        const QPoint item = tl->itemRect(1).center();
        doubleClick(tl, item);
        QVERIFY(m_window->commands()->active());
        QVERIFY(extrude()->isEditing());
        QCOMPARE(field<ValueField>("extrudeDistance")->expression(), QStringLiteral("15"));
        QCOMPARE(field<SelectionField>("extrudeProfiles")->count(), 1);
        QCOMPARE(extrude()->operation(), cad::BodyOperation::NewBody);
        // The timeline shows the model rolled back to the feature being edited.
        QCOMPARE(tl->markerX(), tl->itemRect(1).right() + 4);
        typeInto(field<ValueField>("extrudeDistance"), QStringLiteral("30"));
        settle();
        QVERIFY2(std::fabs(volume(view()->state()) - 24000.0) < 1e-6,
                 qPrintable(QStringLiteral("%1 %2").arg(volume(view()->state())).arg(field<ValueField>("extrudeDistance")->expression())));
        QCOMPARE(int(doc().features().size()), 2);
        panel()->okButton()->click();
        settle();
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Edit Extrude1"));
        QVERIFY(std::fabs(volume(view()->state()) - 24000.0) < 1e-6);
        QCOMPARE(doc().features()[1]->id, eid);
        m_window->undo();
        settle();
        QVERIFY(std::fabs(volume(view()->state()) - 12000.0) < 1e-6);
    }

    void timelineScrubsAndSuppresses() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        extrudeBase(QStringLiteral("10"));
        TimelineWidget *tl = m_window->timeline();
        QCOMPARE(tl->count(), 2);
        // Drag the marker back before the extrude, then forward again.
        const size_t undoBefore = doc().canUndo() ? 1 : 0;
        const QPoint from(tl->markerX(), tl->itemRect(0).center().y());
        const QPoint to(tl->itemRect(0).right() + 4, from.y());
        QTest::mousePress(tl, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(tl, to);
        QCOMPARE(doc().marker(), 1);
        settle();
        QCOMPARE(volume(view()->state()), 0.0);
        QTest::mouseRelease(tl, Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Move History Marker"));
        Q_UNUSED(undoBefore);
        m_window->undo();
        settle();
        QCOMPARE(doc().marker(), 2);
        QVERIFY(std::fabs(volume(view()->state()) - 8000.0) < 1e-6);
        // Suppressing the extrude removes the body; unsuppressing restores it (from the cache).
        doc().setSuppressed(doc().features()[1]->id, true);
        settle();
        QCOMPARE(volume(view()->state()), 0.0);
        const size_t computed = doc().computeCount();
        doc().setSuppressed(doc().features()[1]->id, false);
        settle();
        QVERIFY(std::fabs(volume(view()->state()) - 8000.0) < 1e-6);
        QCOMPARE(doc().computeCount(), computed);
        // Playback buttons step the marker.
        tl->findChild<QToolButton *>(QStringLiteral("timelineFirst"))->click();
        QCOMPARE(doc().marker(), 0);
        tl->findChild<QToolButton *>(QStringLiteral("timelineForward"))->click();
        QCOMPARE(doc().marker(), 1);
        tl->findChild<QToolButton *>(QStringLiteral("timelineLast"))->click();
        QCOMPARE(doc().marker(), 2);
    }

    void browserEyesShowAndHide() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        extrudeBase(QStringLiteral("10"));
        BrowserTree *b = m_window->browser();
        QTreeWidgetItem *bodies = b->folder(QStringLiteral("Bodies"));
        QVERIFY(bodies && bodies->childCount() == 1);
        QTreeWidgetItem *body = bodies->child(0);
        const QRect eye = b->visualItemRect(body).adjusted(b->columnViewportPosition(1), 0, 0, 0);
        QTest::mouseClick(b->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(b->columnViewportPosition(1) + 10, eye.center().y()));
        const cad::BodyId id = view()->state()->bodies.begin()->first;
        QVERIFY(!doc().bodyVisible(id));
        settle();
        QVERIFY(vp()->pickTargets().empty());
        doc().setBodyVisible(id, true);
        settle();
        QCOMPARE(int(vp()->pickTargets().size()), 1);
        // Sketches can be shown again after being used.
        QTreeWidgetItem *sketches = b->folder(QStringLiteral("Sketches"));
        QVERIFY(sketches && sketches->childCount() == 1);
        const cad::FeatureId sid = doc().features()[0]->id;
        QVERIFY(!view()->sketchShown(sid));
        QTreeWidgetItem *sk = sketches->child(0);
        QTest::mouseClick(b->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(b->columnViewportPosition(1) + 10, b->visualItemRect(sk).center().y()));
        QVERIFY(view()->sketchShown(sid));
        // The origin.
        QTreeWidgetItem *origin = b->folder(QStringLiteral("Origin"));
        QVERIFY(!view()->originVisible());
        QTest::mouseClick(b->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(b->columnViewportPosition(1) + 10, b->visualItemRect(origin).center().y()));
        QVERIFY(view()->originVisible());
    }

    void browserFolderAndPlaneEyes() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        extrudeBase(QStringLiteral("10"));
        auto plane = std::make_shared<cad::ConstructionPlaneFeature>();
        plane->base = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        plane->offset = doc().makeSlot("30");
        plane->angle = doc().makeSlot("0");
        const cad::FeatureId pid = doc().addFeature(plane);
        settle();
        QCOMPARE(int(view()->planeQuads().size()), 1);
        BrowserTree *b = m_window->browser();
        auto clickEye = [&](QTreeWidgetItem *item) {
            QTest::mouseClick(b->viewport(), Qt::LeftButton, Qt::NoModifier,
                              QPoint(b->columnViewportPosition(1) + 10, b->visualItemRect(item).center().y()));
            QCoreApplication::processEvents();
            settle();
        };
        // A construction plane's own eye.
        QTreeWidgetItem *construction = b->folder(QStringLiteral("Construction"));
        QVERIFY(construction && construction->childCount() == 1);
        construction->setExpanded(true);
        clickEye(construction->child(0));
        QVERIFY(!doc().planeVisible(pid));
        QVERIFY(view()->planeQuads().empty()); // not drawn, not pickable
        clickEye(b->folder(QStringLiteral("Construction"))->child(0));
        QVERIFY(doc().planeVisible(pid));
        QCOMPARE(int(view()->planeQuads().size()), 1);
        // The Construction folder's eye overrides it, and keeps its setting.
        clickEye(b->folder(QStringLiteral("Construction")));
        QVERIFY(!doc().folderVisible("construction"));
        QVERIFY(view()->planeQuads().empty());
        QVERIFY(doc().planeVisible(pid));
        clickEye(b->folder(QStringLiteral("Construction")));
        QCOMPARE(int(view()->planeQuads().size()), 1);
        // The Bodies folder hides every body; showing it again restores each body's own state.
        const cad::BodyId id = view()->state()->bodies.begin()->first;
        clickEye(b->folder(QStringLiteral("Bodies")));
        QVERIFY(!doc().folderVisible("bodies"));
        QVERIFY(vp()->pickTargets().empty());
        QVERIFY(doc().bodyVisible(id));
        // An item's eye still works while its folder is hidden (it is only overridden).
        clickEye(b->folder(QStringLiteral("Bodies"))->child(0));
        QVERIFY(!doc().bodyVisible(id));
        clickEye(b->folder(QStringLiteral("Bodies")));
        QVERIFY(vp()->pickTargets().empty()); // the body itself is still off
        doc().setBodyVisible(id, true);
        settle();
        QCOMPARE(int(vp()->pickTargets().size()), 1);
        // The Sketches folder.
        const cad::FeatureId sid = doc().features()[0]->id;
        doc().setSketchVisible(sid, true);
        QVERIFY(view()->sketchShown(sid));
        clickEye(b->folder(QStringLiteral("Sketches")));
        QVERIFY(!view()->sketchShown(sid));
        QVERIFY(view()->sketchShown(sid, true));
        // The origin's state is in the document now (saved with the design).
        view()->setOriginVisible(true);
        QVERIFY(doc().folderVisible("origin"));
    }

    void browserRenamesBodies() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        extrudeBase(QStringLiteral("10"));
        BrowserTree *b = m_window->browser();
        QTreeWidgetItem *body = b->folder(QStringLiteral("Bodies"))->child(0);
        doubleClick(b->viewport(), b->visualItemRect(body).center());
        auto *editor = b->viewport()->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->selectAll();
        QTest::keyClicks(editor, QStringLiteral("Plate"));
        QTest::keyClick(editor, Qt::Key_Return); // committed from the event loop
        const cad::Body *first = view()->state()->bodies.begin()->second.get();
        QTRY_COMPARE(QString::fromStdString(doc().bodyName(*first)), QStringLiteral("Plate"));
        QCOMPARE(QString::fromStdString(doc().undoLabel()).left(6), QStringLiteral("Rename"));
    }

    void markingMenuStartsCommands() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        click(at(20, 10, 0));
        const QPointF p = at(20, 10, 0);
        send(vp(), QEvent::MouseButtonPress, p, Qt::RightButton, Qt::RightButton);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::RightButton, Qt::NoButton);
        MarkingMenu *m = m_window->markingMenu();
        QVERIFY(m->isOpen());
        // East is Extrude.
        const QPoint east = m->slotCenter(2);
        QTest::mouseMove(m, east);
        QTest::mouseClick(m, Qt::LeftButton, Qt::NoModifier, east);
        QVERIFY(!m->isOpen());
        QTRY_VERIFY(m_window->commands()->active());
        QCOMPARE(field<SelectionField>("extrudeProfiles")->count(), 1);
        panel()->cancelButton()->click();
        // Esc closes it.
        send(vp(), QEvent::MouseButtonPress, p, Qt::RightButton, Qt::RightButton);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::RightButton, Qt::NoButton);
        QVERIFY(m->isOpen());
        QTest::keyClick(m, Qt::Key_Escape);
        QVERIFY(!m->isOpen());
    }

    void saveNewAndOpen() {
        baseSketch();
        QVERIFY(waitForFrames(vp(), 1));
        extrudeBase(QStringLiteral("12"));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bracket.cadly"));
        QVERIFY(m_window->saveFile(path));
        QVERIFY(m_window->windowTitle().startsWith(QStringLiteral("bracket")));
        m_window->newDocument();
        settle();
        QCOMPARE(int(doc().features().size()), 0);
        QCOMPARE(volume(view()->state()), 0.0);
        QVERIFY(!doc().canUndo());
        QCOMPARE(m_window->timeline()->count(), 0);
        QVERIFY(m_window->openFile(path));
        settle();
        QCOMPARE(int(doc().features().size()), 2);
        QVERIFY(std::fabs(volume(view()->state()) - 9600.0) < 1e-6);
        QCOMPARE(m_window->timeline()->count(), 2);
        QVERIFY(!doc().canUndo());
        // The reopened extrude edits like the original.
        doubleClick(m_window->timeline(), m_window->timeline()->itemRect(1).center());
        QVERIFY(extrude() && extrude()->isEditing());
        QCOMPARE(field<ValueField>("extrudeDistance")->expression(), QStringLiteral("12"));
        panel()->cancelButton()->click();
    }

    void extrudeFromSketchModeFinishesTheSketch() {
        QVERIFY(m_window->sketchMode()->beginNewSketch(cad::PlaneRef::origin(cad::PlaneRef::Kind::XY), false));
        cad::Sketch s;
        s.addCircle(cad::Vec2{0, 0}, 10);
        m_window->sketchMode()->editor()->setSketch(s);
        shortcut(Qt::Key_E, "extrude");
        QVERIFY(!m_window->sketchMode()->active());
        QVERIFY(m_window->commands()->active());
        QCOMPARE(field<SelectionField>("extrudeProfiles")->count(), 1);
        settle();
        QVERIFY(std::fabs(volume(view()->state()) - M_PI * 100 * 10) < 1e-6);
        panel()->okButton()->click();
        settle();
        QCOMPARE(int(doc().features().size()), 2);
    }
};

CADLY_REGISTER_TEST(ModelingTests)

#include "tst_modeling.moc"
