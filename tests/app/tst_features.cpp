// Fillet, Chamfer, Hole, Combine and Offset Plane driven through the UI:
// picking inputs in the canvas (clicks add and remove), live preview, OK,
// Edit Feature reopening the same values, and failures shown in the dialog.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "command/CombineCommand.h"
#include "command/Command.h"
#include "command/CommandPanel.h"
#include "command/DraftCommand.h"
#include "command/EdgeCommands.h"
#include "command/HoleCommand.h"
#include "command/MeasureCommand.h"
#include "command/OverhangCommand.h"
#include "command/PatternCommand.h"
#include "command/PlaneCommand.h"
#include "command/SplitCommand.h"
#include "command/ThreadCommand.h"
#include "ui/AngleDial.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "ui/TimelineWidget.h"
#include "viewport/Viewport.h"

#include "features/ChamferFeature.h"
#include "features/CombineFeature.h"
#include "features/DraftFeature.h"
#include "features/ConstructionPlaneFeature.h"
#include "features/ExtrudeFeature.h"
#include "features/FilletFeature.h"
#include "features/HoleFeature.h"
#include "features/PatternFeature.h"
#include "features/SketchFeature.h"
#include "features/SplitFeature.h"
#include "features/ThreadFeature.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <gp_Pln.hxx>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

#include <cmath>

using namespace cadjitsu;

namespace {

void send(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

double volume(const cad::StatePtr &s) {
    double v = 0;
    for(const auto &kv : s->bodies) v += cad::volumeOf(kv.second->shape.shape());
    return v;
}

void doubleClick(QWidget *w, QPoint pos) {
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, pos);
}

} // namespace

class FeatureTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;

    Viewport *vp() { return m_window->viewport(); }
    cad::Document &doc() { return m_window->document(); }
    ModelView *view() { return m_window->modelView(); }
    CommandPanel *panel() { return m_window->commandPanel(); }
    template <class T> T *command() { return qobject_cast<T *>(m_window->commands()->command()); }
    template <class T> T *field(const char *name) { return panel()->findChild<T *>(QString::fromLatin1(name)); }

    QPointF at(double x, double y, double z) { return vp()->camera().project(QVector3D(float(x), float(y), float(z))); }
    void click(QPointF p) {
        send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
        send(vp(), QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    }
    void settle() {
        QVERIFY(m_window->waitForModel());
        QVERIFY(waitForFrames(vp(), 1));
    }
    void typeInto(ValueField *f, const QString &text) {
        f->setFocus();
        f->selectAll();
        QTest::keyClicks(f, text);
    }
    void trigger(const char *name) {
        QAction *a = m_window->action(QString::fromLatin1(name));
        QVERIFY2(a && a->isEnabled(), name);
        a->trigger();
    }
    double shown() { return volume(view()->state()); }

    // A box made of a rectangle sketch and an extrude, straight into the document.
    cad::FeatureId box(double x0, double y0, double x1, double y1, double height,
                       cad::BodyOperation op = cad::BodyOperation::NewBody) {
        auto s = std::make_shared<cad::SketchFeature>();
        s->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        s->sketch.addRectangle({x0, y0}, {x1, y1});
        const cad::FeatureId sid = doc().addFeature(s);
        auto e = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &p : doc().stateAt(doc().marker())->sketches.at(sid)->profiles) e->profiles.push_back({sid, p.key, p.sample});
        e->distance = doc().makeSlot(std::to_string(height) + " mm");
        e->operation = op;
        return doc().addFeature(e);
    }
    void showHome() {
        m_window->refresh();
        vp()->setStandardView(StandardView::Home, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 1));
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

    void filletPicksEdgesAndPreviews() {
        box(0, 0, 40, 20, 10);
        showHome();
        // Select the top front edge first, then F: the command starts with it.
        click(at(20, 0, 10));
        QCOMPARE(view()->selection().count(SelectionItem::Kind::Edge), size_t(1));
        trigger("fillet");
        auto *fillet = command<FilletCommand>();
        QVERIFY(fillet);
        QCOMPARE(fillet->edgeCount(), 1);
        QCOMPARE(field<SelectionField>("edges")->count(), 1);
        typeInto(fillet->radiusField(), QStringLiteral("2"));
        settle();
        const double one = 8000.0 - (1.0 - M_PI / 4) * 4.0 * 40.0;
        QVERIFY2(std::fabs(shown() - one) < 1e-3, qPrintable(QString::number(shown())));
        // Clicking another edge adds it (no modifier needed in a command).
        click(at(40, 10, 10));
        settle();
        QCOMPARE(fillet->edgeCount(), 2);
        QVERIFY(shown() < one - 1.0);
        // The first edge is rounded away in the preview, but its mark can still be clicked.
        click(at(20, 0, 10));
        settle();
        QCOMPARE(fillet->edgeCount(), 1);
        QVERIFY(std::fabs(shown() - (8000.0 - (1.0 - M_PI / 4) * 4.0 * 20.0)) < 1e-3);
        panel()->okButton()->click();
        settle();
        QCOMPARE(int(doc().features().size()), 3);
        const auto f = std::dynamic_pointer_cast<const cad::FilletFeature>(doc().features().back());
        QVERIFY(f && f->edges.size() == 1 && f->radius.expr == "2 mm");
        // Edit Feature: same dialog, same values.
        TimelineWidget *tl = m_window->timeline();
        doubleClick(tl, tl->itemRect(2).center());
        fillet = command<FilletCommand>();
        QVERIFY(fillet && fillet->isEditing());
        QCOMPARE(fillet->edgeCount(), 1);
        QCOMPARE(fillet->radiusField()->expression(), QStringLiteral("2 mm"));
        panel()->cancelButton()->click();
    }

    void aFailingFilletIsAnErrorNotACrash() {
        box(0, 0, 40, 20, 10);
        showHome();
        trigger("fillet");
        click(at(20, 0, 10));
        typeInto(command<FilletCommand>()->radiusField(), QStringLiteral("30"));
        settle();
        QVERIFY(!panel()->message().isEmpty());
        QVERIFY(!panel()->okButton()->isEnabled());
        QVERIFY(std::fabs(shown() - 8000.0) < 1e-6); // the model before the fillet
        // Enter does not commit it either.
        QTest::keyClick(command<FilletCommand>()->radiusField(), Qt::Key_Return);
        QVERIFY(m_window->commands()->active());
        typeInto(command<FilletCommand>()->radiusField(), QStringLiteral("1"));
        settle();
        QVERIFY(panel()->okButton()->isEnabled());
        panel()->okButton()->click();
        settle();
        QCOMPARE(doc().statusOf(doc().features().back()->id).severity, cad::Severity::Ok);
    }

    void chamferAFaceWithEachType() {
        box(0, 0, 40, 20, 10);
        showHome();
        trigger("chamfer");
        auto *chamfer = command<ChamferCommand>();
        QVERIFY(chamfer);
        click(at(20, 10, 10)); // the top face: all four of its edges
        QCOMPARE(chamfer->edgeCount(), 1);
        typeInto(chamfer->distanceField(), QStringLiteral("1"));
        settle();
        const double equal = shown();
        QVERIFY2(equal < 8000.0 - 55.0 && equal > 8000.0 - 65.0, qPrintable(QString::number(equal)));
        QVERIFY(!chamfer->distance2Field()->isVisible() && !chamfer->angleField()->isVisible());
        chamfer->typeBox()->setCurrentIndex(1); // two distances
        QVERIFY(chamfer->distance2Field()->isVisible());
        typeInto(chamfer->distance2Field(), QStringLiteral("2"));
        settle();
        QVERIFY(shown() < equal - 10.0);
        chamfer->typeBox()->setCurrentIndex(2); // distance and angle
        QVERIFY(chamfer->angleField()->isVisible());
        settle();
        QVERIFY(std::fabs(shown() - equal) < 1.0); // 1 mm at 45 degrees is the equal chamfer
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::ChamferFeature>(doc().features().back());
        QVERIFY(f && f->chamferType == cad::ChamferType::DistanceAngle && f->faces.size() == 1);
        QCOMPARE(doc().statusOf(f->id).severity, cad::Severity::Ok);
    }

    void holesAreClickedOntoAFace() {
        box(0, 0, 40, 20, 10);
        showHome();
        trigger("hole");
        auto *hole = command<HoleCommand>();
        QVERIFY(hole);
        typeInto(hole->depthField(), QStringLiteral("5"));
        click(at(10, 10, 10));
        settle();
        QCOMPARE(hole->holeCount(), 1);
        QVERIFY(std::fabs(hole->points()[0].x - 10.0) < 0.3 && std::fabs(hole->points()[0].y - 10.0) < 0.3);
        // X / Y place it exactly.
        typeInto(hole->xField(), QStringLiteral("10"));
        typeInto(hole->yField(), QStringLiteral("10"));
        settle();
        QCOMPARE(hole->points()[0].x, 10.0);
        const double tip = 2.5 / std::tan(59.0 * M_PI / 180.0);
        const double one = M_PI * 6.25 * 5.0 + M_PI * 6.25 * tip / 3.0;
        QVERIFY2(std::fabs(shown() - (8000.0 - one)) < 1e-3, qPrintable(QString::number(8000.0 - shown())));
        QVERIFY(view()->evaluation()->tool); // the drill is shown translucent
        // A second click adds a hole; clicking a hole's centre mark removes it.
        click(at(30, 10, 10));
        settle();
        QCOMPARE(hole->holeCount(), 2);
        click(at(10, 10, 10));
        settle();
        QCOMPARE(hole->holeCount(), 1);
        QVERIFY(std::fabs(hole->points()[0].x - 30.0) < 0.3);
        // Through all.
        hole->extentBox()->setCurrentIndex(1);
        settle();
        QVERIFY(std::fabs(shown() - (8000.0 - M_PI * 6.25 * 10.0)) < 1e-3);
        hole->typeBox()->setCurrentIndex(1); // counterbore
        QVERIFY(field<ValueField>("holeCboreDiameter")->isVisible());
        settle();
        QVERIFY(shown() < 8000.0 - M_PI * 6.25 * 10.0 - 1.0);
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::HoleFeature>(doc().features().back());
        QVERIFY(f && f->points.size() == 1 && f->holeType == cad::HoleType::Counterbore);
        // Edit Feature reopens it.
        m_window->editFeature(f->id);
        hole = command<HoleCommand>();
        QVERIFY(hole && hole->isEditing());
        QCOMPARE(hole->holeCount(), 1);
        QCOMPARE(hole->typeBox()->currentIndex(), 1);
        QCOMPARE(hole->extentBox()->currentIndex(), 1);
        panel()->cancelButton()->click();
    }

    void holesAtSketchPoints() {
        box(0, 0, 40, 20, 10);
        // A sketch of two points on the top face.
        const cad::StatePtr st = doc().stateAt(doc().marker());
        const cad::Body *b = st->bodies.begin()->second.get();
        int top = 0;
        for(int i = 1; i <= b->shape.faceCount(); ++i) {
            gp_Pln pln;
            if(cad::planeOfFace(b->shape.face(i), pln) && pln.Axis().Direction().IsEqual(gp_Dir(0, 0, 1), 1e-9)) top = i;
        }
        auto s = std::make_shared<cad::SketchFeature>();
        s->plane = cad::PlaneRef::onFace(cad::makeTopoRef(*b, cad::TopoKind::Face, top));
        s->sketch.addPoint(10, 10);
        s->sketch.addPoint(30, 10);
        const cad::FeatureId sid = doc().addFeature(s);
        showHome();
        trigger("hole");
        auto *hole = command<HoleCommand>();
        QVERIFY(hole);
        hole->placementBox()->setCurrentIndex(1);
        QVERIFY(view()->filter().sketchPoints);
        click(at(10, 10, 10));
        click(at(30, 10, 10));
        QCOMPARE(hole->holeCount(), 2);
        hole->tipBox()->setCurrentIndex(1); // flat
        typeInto(hole->depthField(), QStringLiteral("4"));
        settle();
        QVERIFY2(std::fabs(shown() - (8000.0 - 2 * M_PI * 6.25 * 4.0)) < 1e-3, qPrintable(QString::number(shown())));
        // Clicking a picked point again drops it.
        click(at(30, 10, 10));
        settle();
        QCOMPARE(hole->holeCount(), 1);
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::HoleFeature>(doc().features().back());
        QVERIFY(f && f->sketch == sid && f->sketchPoints.size() == 1 && f->flatTip);
        QVERIFY(std::fabs(shown() - (8000.0 - M_PI * 6.25 * 4.0)) < 1e-3);
        // The sketch is used now, so it is hidden.
        QVERIFY(!view()->sketchShown(sid));
    }

    void combineWithAndWithoutKeepingTools() {
        box(0, 0, 40, 20, 10);
        box(30, 0, 50, 20, 10); // overlaps the first by 10 x 20 x 10; a new body
        showHome();
        QVERIFY2(view()->state()->bodies.size() == 2,
                 qPrintable(QStringLiteral("%1 bodies, %2 mm3, %3").arg(view()->state()->bodies.size()).arg(shown())
                                .arg(QString::fromStdString(doc().statusOf(doc().features().back()->id).message))));
        trigger("combine");
        auto *combine = command<CombineCommand>();
        QVERIFY(combine);
        click(at(10, 10, 10)); // the target
        QVERIFY(combine->hasTarget());
        click(at(48, 10, 10)); // a tool (the target field hands over to the tools)
        QCOMPARE(combine->toolCount(), 1);
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 1);
        QVERIFY(std::fabs(shown() - 10000.0) < 1e-3);
        combine->operationBox()->setCurrentIndex(1); // cut
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 1);
        QVERIFY(std::fabs(shown() - 6000.0) < 1e-3);
        QVERIFY(view()->evaluation()->tool); // the tool that goes away is shown translucent
        combine->keepToolsBox()->setChecked(true);
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 2);
        QVERIFY(std::fabs(shown() - 10000.0) < 1e-3);
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::CombineFeature>(doc().features().back());
        QVERIFY(f && f->operation == cad::BodyOperation::Cut && f->keepTools && f->tools.size() == 1);
    }

    void offsetPlaneFromAFaceTurnedByAnAngle() {
        box(0, 0, 40, 20, 10);
        showHome();
        trigger("offsetPlane");
        auto *plane = command<PlaneCommand>();
        QVERIFY(plane);
        // The origin planes are shown to pick from.
        QVERIFY(view()->planeQuads().size() >= 3);
        click(at(20, 10, 10)); // the top face
        QVERIFY(plane->hasBase());
        typeInto(plane->offsetField(), QStringLiteral("15"));
        settle();
        QCOMPARE(int(view()->state()->planes.size()), 1);
        const cad::PlaneResult *p = view()->state()->planes.begin()->second.get();
        QVERIFY(std::fabs(p->frame.Location().Z() - 25.0) < 1e-9);
        QVERIFY(p->frame.Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));
        QVERIFY(p->center.Distance(gp_Pnt(20, 10, 25)) < 1e-6); // drawn over the face
        // Turned 30 degrees about its X axis.
        typeInto(plane->angleField(), QStringLiteral("30"));
        settle();
        p = view()->state()->planes.begin()->second.get();
        QVERIFY(std::fabs(p->frame.Direction().Angle(gp_Dir(0, 0, 1)) - M_PI / 6) < 1e-9);
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::ConstructionPlaneFeature>(doc().features().back());
        QVERIFY(f && f->base.kind == cad::PlaneRef::Kind::Face && f->offset.expr == "15 mm" && f->angle.expr == "30 deg");
        // An origin plane works as a base too; Edit Feature reopens with the values.
        m_window->editFeature(f->id);
        plane = command<PlaneCommand>();
        QVERIFY(plane && plane->isEditing() && plane->hasBase());
        QCOMPARE(plane->offsetField()->expression(), QStringLiteral("15 mm"));
        QCOMPARE(plane->angleField()->expression(), QStringLiteral("30 deg"));
        panel()->cancelButton()->click();
        settle();
        trigger("offsetPlane");
        plane = command<PlaneCommand>();
        vp()->setStandardView(StandardView::Top, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 1));
        const auto quads = view()->planeQuads();
        QPointF xyCorner;
        for(const auto &[item, q] : quads)
            if(item.key == "XY") xyCorner = vp()->camera().project(q[2] * 0.95f);
        click(xyCorner); // outside the box, on the XY plane's square
        QVERIFY(plane->hasBase());
        settle();
        QCOMPARE(int(view()->state()->planes.size()), 2);
    }

    void offsetPlaneTiltsOnBothAxesWithRingsAndDials() {
        box(0, 0, 40, 20, 10);
        showHome();
        trigger("offsetPlane");
        auto *plane = command<PlaneCommand>();
        QVERIFY(plane);
        click(at(20, 10, 10)); // the top face
        QVERIFY(plane->hasBase());
        typeInto(plane->offsetField(), QStringLiteral("5"));
        typeInto(plane->angleField(), QStringLiteral("30"));
        typeInto(plane->tiltYField(), QStringLiteral("20"));
        settle();
        auto normalTilt = [&] {
            const cad::PlaneResult *p = view()->state()->planes.begin()->second.get();
            return p->frame.Direction().Angle(gp_Dir(0, 0, 1));
        };
        auto centre = [&] { return view()->state()->planes.begin()->second->center; };
        // Tilted about X then about the new Y: cos(tilt) = cos(30) cos(20); it
        // turns about its centre, over the face's middle.
        QVERIFY(std::fabs(std::cos(normalTilt()) - std::cos(M_PI / 6) * std::cos(M_PI / 9)) < 1e-9);
        QVERIFY(centre().Distance(gp_Pnt(20, 10, 15)) < 1e-6);
        // The dials show the typed values.
        QCOMPARE(qRound(panel()->angleDial(plane->angleField())->angle()), 30);
        QCOMPARE(qRound(panel()->angleDial(plane->tiltYField())->angle()), 20);

        // Drag the Tilt X ring's knob round to 60 degrees.
        PlaneGizmo &g = plane->gizmo();
        QVERIFY(g.ring(0).visible && g.ring(1).visible);
        QVERIFY(std::fabs(g.ring(0).angle - M_PI / 6) < 1e-9);
        auto dragRing = [&](int ring, double toDeg, Qt::KeyboardModifiers mods) {
            const QPointF from = g.knobOnScreen(ring);
            const double a0 = g.ring(ring).angle, a1 = toDeg * M_PI / 180.0;
            send(vp(), QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
            send(vp(), QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
            QCOMPARE(g.dragged(), ring ? PlaneGizmo::Part::Ring1 : PlaneGizmo::Part::Ring0);
            QPointF p;
            for(int i = 1; i <= 12; ++i) {
                p = g.ringPointOnScreen(ring, a0 + (a1 - a0) * i / 12.0);
                QMouseEvent ev(QEvent::MouseMove, p, vp()->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, mods);
                QCoreApplication::sendEvent(vp(), &ev);
            }
            send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
            QCOMPARE(g.dragged(), PlaneGizmo::Part::None);
        };
        dragRing(0, 60.0, Qt::NoModifier);
        QCOMPARE(plane->angleField()->expression(), QStringLiteral("60 deg"));
        QCOMPARE(qRound(panel()->angleDial(plane->angleField())->angle()), 60);
        settle();
        QVERIFY(std::fabs(std::cos(normalTilt()) - std::cos(M_PI / 3) * std::cos(M_PI / 9)) < 1e-9);
        // With Shift it snaps to 15 degrees: 97 -> 90 on the Tilt Y ring.
        dragRing(1, 97.0, Qt::ShiftModifier);
        QCOMPARE(plane->tiltYField()->expression(), QStringLiteral("90 deg"));
        settle();

        // Hovering a ring puts typed digits into its tilt.
        send(vp(), QEvent::MouseMove, g.knobOnScreen(1), Qt::NoButton, Qt::NoButton);
        QCOMPARE(g.focus(), PlaneGizmo::Part::Ring1);
        QCOMPARE(plane->canvasValue(), plane->tiltYField());
        QVERIFY(waitForFrames(vp(), 1));
        auto *canvasBox = vp()->findChild<QLineEdit *>(QStringLiteral("canvasValue"));
        QVERIFY(canvasBox && canvasBox->isVisible());
        vp()->setFocus();
        QTest::keyClick(vp(), Qt::Key_4, Qt::NoModifier);
        QTest::keyClicks(canvasBox, QStringLiteral("5"));
        QCOMPARE(plane->tiltYField()->expression(), QStringLiteral("45 deg"));
        // Hovering the arrow puts them back into the distance.
        send(vp(), QEvent::MouseMove, g.arrow().headOnScreen(), Qt::NoButton, Qt::NoButton);
        QCOMPARE(plane->canvasValue(), plane->offsetField());

        // The dial: scroll a degree, drag to straight up (90), double-click for 0.
        AngleDial *dial = panel()->angleDial(plane->tiltYField());
        QVERIFY(dial && dial->isVisible());
        const QPointF mid = QRectF(dial->rect()).center();
        QWheelEvent wheel(mid, dial->mapToGlobal(mid), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(dial, &wheel);
        QCOMPARE(plane->tiltYField()->expression(), QStringLiteral("46 deg"));
        QTest::mouseMove(dial, (mid + QPointF(0, -15)).toPoint());
        QTest::mousePress(dial, Qt::LeftButton, Qt::NoModifier, (mid + QPointF(0, -15)).toPoint());
        QTest::mouseRelease(dial, Qt::LeftButton, Qt::NoModifier, (mid + QPointF(0, -15)).toPoint());
        QCOMPARE(plane->tiltYField()->expression(), QStringLiteral("90 deg"));
        doubleClick(dial, (mid + QPointF(0, -15)).toPoint());
        QCOMPARE(plane->tiltYField()->expression(), QStringLiteral("0 deg"));
        settle();
        QVERIFY(std::fabs(normalTilt() - M_PI / 3) < 1e-9);

        // Edge mode: one angle, one ring.
        plane->axisBox()->setCurrentIndex(1);
        QVERIFY(!plane->tiltYField()->isVisibleTo(panel()) && !g.ring(1).visible);
        plane->axisBox()->setCurrentIndex(0);
        QVERIFY(plane->tiltYField()->isVisibleTo(panel()) && g.ring(1).visible);

        typeInto(plane->tiltYField(), QStringLiteral("-25"));
        settle();
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::ConstructionPlaneFeature>(doc().features().back());
        QVERIFY(f && f->axis == cad::PlaneRotationAxis::LocalX && f->pivotAtCenter);
        QCOMPARE(QString::fromStdString(f->angle.expr), QStringLiteral("60 deg"));
        QCOMPARE(QString::fromStdString(f->angleY.expr), QStringLiteral("-25 deg"));
        // Edit Feature reopens both tilts.
        m_window->editFeature(f->id);
        plane = command<PlaneCommand>();
        QVERIFY(plane && plane->isEditing());
        QCOMPARE(plane->angleField()->expression(), QStringLiteral("60 deg"));
        QCOMPARE(plane->tiltYField()->expression(), QStringLiteral("-25 deg"));
        panel()->cancelButton()->click();
        settle();
    }

    void planesFromBeforeTwoAxisTiltsEditUnchanged() {
        auto legacy = std::make_shared<cad::ConstructionPlaneFeature>();
        legacy->base = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        legacy->offset = doc().makeSlot("10 mm");
        legacy->angle = doc().makeSlot("90 deg");
        legacy->axis = cad::PlaneRotationAxis::LocalY;
        legacy->pivotAtCenter = false;
        const cad::FeatureId id = doc().addFeature(legacy);
        showHome();
        auto check = [&] {
            const cad::PlaneResult *p = doc().stateAt(doc().marker())->planes.at(id).get();
            QVERIFY(p->frame.Direction().IsParallel(gp_Dir(1, 0, 0), 1e-9));
            QVERIFY(p->frame.Location().Distance(gp_Pnt(0, 0, 10)) < 1e-9);
        };
        check();
        // It opens with its angle as Tilt Y, and OK keeps the plane where it was.
        m_window->editFeature(id);
        auto *plane = command<PlaneCommand>();
        QVERIFY(plane);
        QCOMPARE(plane->tiltYField()->expression(), QStringLiteral("90 deg"));
        QCOMPARE(plane->angleField()->expression(), QStringLiteral("0 deg"));
        settle();
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::ConstructionPlaneFeature>(doc().feature(id));
        QVERIFY(f && f->axis == cad::PlaneRotationAxis::LocalX && !f->pivotAtCenter);
        check();
    }

    void measureBetweenFacesPointsAndBodies() {
        box(0, 0, 40, 20, 10);
        box(60, 0, 80, 20, 10);
        showHome();
        QCOMPARE(m_window->action(QStringLiteral("measure"))->shortcut(), QKeySequence(Qt::Key_I));
        trigger("measure");
        auto *m = command<MeasureCommand>();
        QVERIFY(m);
        // The two top faces: 20 mm apart, parallel.
        click(at(20, 10, 10));
        QCOMPARE(m->targetCount(), 1);
        QVERIFY(panel()->findChild<QLabel *>(QStringLiteral("measureProperties"))->text().contains(QStringLiteral("Area")));
        click(at(70, 10, 10));
        QCOMPARE(m->targetCount(), 2);
        QVERIFY(m->result() && m->result()->ok);
        QVERIFY(std::fabs(m->result()->distance - 20.0) < 1e-6);
        QCOMPARE(m->distanceLabel()->text(), QStringLiteral("20.000 mm"));
        QVERIFY(m->result()->angle && std::fabs(*m->result()->angle) < 1e-9);
        QVERIFY(m->resultText().contains(QStringLiteral("Distance: 20.000 mm")));
        // Inches.
        m->unitsBox()->setCurrentIndex(1);
        QCOMPARE(m->distanceLabel()->text(), QStringLiteral("0.7874 in"));
        m->unitsBox()->setCurrentIndex(0);
        // Empty space starts again.
        click(QPointF(vp()->width() - 30, vp()->height() - 30));
        QCOMPARE(m->targetCount(), 0);
        // Points on surfaces: a ruler between two clicked points.
        m->modeBox()->setCurrentIndex(2);
        click(at(10, 10, 10));
        click(at(70, 10, 10));
        QVERIFY(m->result() && m->result()->ok);
        QVERIFY2(std::fabs(m->result()->distance - 60.0) < 0.5, qPrintable(QString::number(m->result()->distance)));
        // Whole bodies.
        m->modeBox()->setCurrentIndex(1);
        click(at(20, 10, 10));
        click(at(70, 10, 10));
        QVERIFY(m->result() && std::fabs(m->result()->distance - 20.0) < 1e-6);
        panel()->cancelButton()->click();
        QVERIFY(!command<MeasureCommand>());
    }

    void overhangAnalysisColoursWhatNeedsSupport() {
        // A column with a slab resting on top of it: the slab's underside is a flat bridge.
        box(10, 0, 30, 20, 10);
        auto p = std::make_shared<cad::ConstructionPlaneFeature>();
        p->base = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        p->offset = doc().makeSlot("10 mm");
        const cad::FeatureId pid = doc().addFeature(p);
        auto s = std::make_shared<cad::SketchFeature>();
        s->plane = cad::PlaneRef::construction(pid);
        s->sketch.addRectangle({0, 0}, {40, 20});
        const cad::FeatureId sid = doc().addFeature(s);
        auto e = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &pr : doc().stateAt(doc().marker())->sketches.at(sid)->profiles) e->profiles.push_back({sid, pr.key, pr.sample});
        e->distance = doc().makeSlot("5 mm");
        e->operation = cad::BodyOperation::NewBody;
        doc().addFeature(e);
        showHome();
        settle();
        QVERIFY(!view()->overhangAnalysis());
        trigger("overhangs");
        auto *o = command<OverhangCommand>();
        QVERIFY(o);
        settle();
        QVERIFY(view()->overhangAnalysis());
        const auto &a = view()->overhangAreas();
        QVERIFY2(std::fabs(a[size_t(cad::OverhangKind::Bridge)] - 800.0) < 1e-3,
                 qPrintable(QString::number(a[size_t(cad::OverhangKind::Bridge)])));
        QVERIFY(std::fabs(a[size_t(cad::OverhangKind::Plate)] - 400.0) < 1e-3);
        QVERIFY(a[size_t(cad::OverhangKind::Overhang)] < 1e-9);
        QCOMPARE(o->supportLabel()->text(), QStringLiteral("0.0 mm²"));
        typeInto(o->limitField(), QStringLiteral("30"));
        settle();
        QVERIFY(std::fabs(view()->overhangAreas()[size_t(cad::OverhangKind::Bridge)] - 800.0) < 1e-3);
        panel()->okButton()->click();
        QVERIFY(!view()->overhangAnalysis());
    }

    void splitBodyWithAPlaneAndWithASketchLine() {
        box(0, 0, 40, 20, 10);
        auto p = std::make_shared<cad::ConstructionPlaneFeature>();
        p->base = cad::PlaneRef::origin(cad::PlaneRef::Kind::YZ);
        p->offset = doc().makeSlot("15 mm");
        const cad::FeatureId pid = doc().addFeature(p);
        auto line = std::make_shared<cad::SketchFeature>();
        line->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        line->sketch.addLine(cad::Vec2{30, -5}, cad::Vec2{30, 25});
        const cad::FeatureId lid = doc().addFeature(line);
        showHome();
        settle();
        trigger("split");
        auto *sp = command<SplitCommand>();
        QVERIFY(sp);
        // Click the construction plane's square away from the box.
        for(const auto &[item, q] : view()->planeQuads())
            if(item.kind == SelectionItem::Kind::Plane && item.feature == pid)
                for(int k = 0; k < 4 && !sp->hasPlane(); ++k) // a corner the box does not hide
                    click(vp()->camera().project(q[size_t(k)] * 0.85f + q[size_t((k + 2) % 4)] * 0.15f));
        QVERIFY(sp->hasPlane());
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 2);
        QVERIFY(std::fabs(shown() - 8000.0) < 1e-3);
        // Keep the front side only (x > 15).
        sp->keepBox()->setCurrentIndex(1);
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 1);
        QVERIFY(std::fabs(shown() - 25 * 20 * 10) < 1e-3);
        // Both sides with alignment pins: two 3.2 x 6 mm holes in each half.
        sp->keepBox()->setCurrentIndex(0);
        sp->pinsBox()->setChecked(true);
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 2);
        const double hole = M_PI * 1.6 * 1.6 * 6;
        QVERIFY2(std::fabs(shown() - (8000 - 4 * hole)) < 1e-3, qPrintable(QString::number(shown())));
        // With the sketch line instead.
        sp->pinsBox()->setChecked(false);
        sp->toolBox()->setCurrentIndex(1);
        QVERIFY(!field<QWidget>("splitKeep")->isVisibleTo(panel()));
        settle();
        click(at(30, -2, 0)); // on the line, outside the box
        QCOMPARE(sp->curveCount(), 1);
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 2);
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::SplitFeature>(doc().features().back());
        QVERIFY(f && f->tool == cad::SplitTool::Sketch && f->sketch == lid && f->curves.size() == 1);
        QCOMPARE(int(view()->state()->bodies.size()), 2);
        // Edit reopens it.
        m_window->editFeature(f->id);
        sp = command<SplitCommand>();
        QVERIFY(sp && sp->curveCount() == 1 && sp->toolBox()->currentIndex() == 1);
        panel()->cancelButton()->click();
        settle();
    }

    void draftAWallAboutItsFootWithTheRing() {
        box(0, 0, 40, 20, 10);
        showHome();
        trigger("draft");
        auto *d = command<DraftCommand>();
        QVERIFY(d);
        click(at(40, 10, 5)); // the +X wall
        QCOMPARE(d->faceCount(), 1);
        QVERIFY(!d->hasHinge());
        click(at(40, 10, 0)); // its foot
        QVERIFY(d->hasHinge());
        settle();
        auto wedge = [](double deg) { return 0.5 * 10 * 10 * std::tan(deg * M_PI / 180.0) * 20; };
        QVERIFY2(std::fabs(shown() - (8000 - wedge(5))) < 1e-3, qPrintable(QString::number(shown())));
        // Turn the ring on the hinge to 10 degrees.
        PlaneGizmo &g = d->gizmo();
        QVERIFY(g.ring(0).visible);
        const QPointF from = g.knobOnScreen(0);
        send(vp(), QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
        send(vp(), QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        QPointF p;
        for(int i = 1; i <= 10; ++i) {
            p = g.ringPointOnScreen(0, (5.0 + 0.5 * i) * M_PI / 180.0);
            send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton);
        }
        send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(d->angleField()->expression(), QStringLiteral("10 deg"));
        settle();
        QVERIFY(std::fabs(shown() - (8000 - wedge(10))) < 1e-3);
        // Leaning out adds the wedge instead.
        d->flipBox()->setChecked(true);
        settle();
        QVERIFY(std::fabs(shown() - (8000 + wedge(10))) < 1e-3);
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::DraftFeature>(doc().features().back());
        QVERIFY(f && f->flip && f->angle.expr == "10 deg" && f->faces.size() == 1);
        m_window->editFeature(f->id);
        d = command<DraftCommand>();
        QVERIFY(d && d->hasHinge() && d->faceCount() == 1);
        panel()->cancelButton()->click();
        settle();
    }

    void patternsOfBodiesAndABoltCircleOfHoles() {
        // A 10 x 10 block: three in a row, 15 mm apart.
        box(0, 0, 10, 10, 5);
        showHome();
        trigger("patternRect");
        auto *p = command<PatternCommand>();
        QVERIFY(p);
        click(at(5, 5, 5));
        QCOMPARE(p->bodyCount(), 1);
        typeInto(p->count1Field(), QStringLiteral("3"));
        typeInto(p->spacing1Field(), QStringLiteral("15"));
        p->joinBox()->setChecked(false);
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 3);
        QVERIFY(std::fabs(shown() - 3 * 500) < 1e-6);
        // A second direction makes a grid.
        p->secondBox()->setChecked(true);
        settle();
        QCOMPARE(int(view()->state()->bodies.size()), 6);
        panel()->okButton()->click();
        settle();
        QCOMPARE(int(doc().features().size()), 3);

        // A disc with a hole, and the hole repeated around Z.
        m_window->newDocument();
        auto s = std::make_shared<cad::SketchFeature>();
        s->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        s->sketch.addCircle(cad::Vec2{0, 0}, 30);
        const cad::FeatureId sid = doc().addFeature(s);
        auto e = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &pr : doc().stateAt(1)->sketches.at(sid)->profiles) e->profiles.push_back({sid, pr.key, pr.sample});
        e->distance = doc().makeSlot("5 mm");
        doc().addFeature(e);
        const cad::Body &disc = *doc().displayedState()->bodies.begin()->second;
        auto h = std::make_shared<cad::HoleFeature>();
        for(int i = 1; i <= disc.shape.faceCount(); ++i) {
            gp_Pln pl;
            if(cad::planeOfFace(disc.shape.face(i), pl) && pl.Axis().Direction().Z() > 0.9)
                h->face = cad::makeTopoRef(disc, cad::TopoKind::Face, i);
        }
        h->points = {{20, 0}};
        h->extent = cad::ExtentType::ThroughAll;
        h->diameter = doc().makeSlot("6 mm");
        const cad::FeatureId hid = doc().addFeature(h);
        showHome();
        settle();
        const double before = shown();
        trigger("patternCircular");
        p = command<PatternCommand>();
        QVERIFY(p);
        p->objectsBox()->setCurrentIndex(1); // features
        // Click into the hole until its wall is hit.
        const QPointF c = at(20, 0, 5);
        for(int dx = -6; dx <= 6 && p->featureCount() == 0; dx += 2)
            for(int dy = -6; dy <= 6 && p->featureCount() == 0; dy += 2) click(c + QPointF(dx, dy));
        QCOMPARE(p->featureCount(), 1);
        typeInto(p->countField(), QStringLiteral("5"));
        settle();
        const double hole = M_PI * 3 * 3 * 5;
        QVERIFY2(std::fabs(shown() - (before - 4 * hole)) < 1e-3, qPrintable(QString::number(before - shown())));
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::PatternFeature>(doc().features().back());
        QVERIFY(f && f->kind == cad::PatternKind::Circular && f->features == std::vector<cad::FeatureId>{hid});
        m_window->editFeature(f->id);
        p = command<PatternCommand>();
        QVERIFY(p && p->featureCount() == 1 && p->countField()->expression() == QStringLiteral("5"));
        panel()->cancelButton()->click();
        settle();
    }

    void threadAHoleAndTapAnother() {
        box(0, 0, 20, 20, 10);
        const cad::Body &plate = *doc().displayedState()->bodies.begin()->second;
        auto h = std::make_shared<cad::HoleFeature>();
        for(int i = 1; i <= plate.shape.faceCount(); ++i) {
            gp_Pln pl;
            if(cad::planeOfFace(plate.shape.face(i), pl) && pl.Axis().Direction().Z() > 0.9)
                h->face = cad::makeTopoRef(plate, cad::TopoKind::Face, i);
        }
        h->points = {{10, 10}};
        h->extent = cad::ExtentType::ThroughAll;
        h->diameter = doc().makeSlot("5 mm");
        doc().addFeature(h);
        showHome();
        settle();
        const double drilled = shown();
        trigger("thread");
        auto *t = command<ThreadCommand>();
        QVERIFY(t);
        const QPointF c = at(10, 10, 10);
        for(int r = 0; r <= 8 && t->faceCount() == 0; ++r)
            for(int k = 0; k < 8 && t->faceCount() == 0; ++k)
                click(c + QPointF(r * std::cos(k * M_PI / 4), r * std::sin(k * M_PI / 4)));
        QCOMPARE(t->faceCount(), 1);
        QVERIFY2(t->detectedLabel()->text().contains(QStringLiteral("M6")), qPrintable(t->detectedLabel()->text()));
        settle();
        QVERIFY(shown() < drilled - 5.0); // the M6 thread cut into the wall
        // Plain: a 5 mm hole is already the M6 tap drill; nothing changes.
        t->modeBox()->setCurrentIndex(2);
        settle();
        QVERIFY(std::fabs(shown() - drilled) < 1e-6);
        t->modeBox()->setCurrentIndex(0);
        panel()->okButton()->click();
        settle();
        const auto f = std::dynamic_pointer_cast<const cad::ThreadFeature>(doc().features().back());
        QVERIFY(f && f->faces.size() == 1 && f->mode == cad::ThreadMode::Auto);
        QVERIFY(doc().statusOf(f->id).isOk());
        m_window->editFeature(f->id);
        t = command<ThreadCommand>();
        QVERIFY(t && t->faceCount() == 1);
        panel()->cancelButton()->click();
        settle();

        // A tapped M5 hole from the Hole dialog.
        const double before = shown();
        trigger("hole");
        auto *hole = command<HoleCommand>();
        QVERIFY(hole);
        hole->typeBox()->setCurrentIndex(3);
        hole->threadSizeBox()->setCurrentText(QStringLiteral("M5"));
        hole->extentBox()->setCurrentIndex(1);
        click(at(4, 4, 10));
        settle();
        QVERIFY2(hole->threadInfo()->text().contains(QStringLiteral("M5")), qPrintable(hole->threadInfo()->text()));
        QVERIFY(!field<QWidget>("holeDiameter")->isVisibleTo(panel()));
        // Less than the 4.2 mm tap drill alone would remove? More: the thread cuts further.
        QVERIFY(shown() < before - M_PI * 2.1 * 2.1 * 10);
        panel()->okButton()->click();
        settle();
        const auto th = std::dynamic_pointer_cast<const cad::HoleFeature>(doc().features().back());
        QVERIFY(th && th->holeType == cad::HoleType::Tapped && th->thread == "M5");
    }

    void markingMenuAndShortcutsReachTheNewCommands() {
        box(0, 0, 40, 20, 10);
        showHome();
        QCOMPARE(m_window->action(QStringLiteral("fillet"))->shortcut(), QKeySequence(Qt::Key_F));
        QCOMPARE(m_window->action(QStringLiteral("hole"))->shortcut(), QKeySequence(Qt::Key_H));
        for(const char *name : {"fillet", "chamfer", "hole", "combine", "offsetPlane"})
            QVERIFY2(m_window->action(QString::fromLatin1(name))->isEnabled(), name);
        trigger("hole");
        // While a command runs, the others wait.
        QVERIFY(!m_window->action(QStringLiteral("fillet"))->isEnabled());
        panel()->cancelButton()->click();
        QVERIFY(m_window->action(QStringLiteral("fillet"))->isEnabled());
    }
};

CADJITSU_REGISTER_TEST(FeatureTests)

#include "tst_features.moc"
