// Viewport: camera math, Fusion 360 mouse navigation, picking and selection.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "model/ModelView.h"
#include "selftest/DemoModels.h"
#include "selftest/TestUtil.h"
#include "viewport/Camera.h"
#include "viewport/ViewCube.h"
#include "viewport/Viewport.h"

#include "topo/NamedShape.h"

#include <QLabel>
#include <QtTest>

using namespace cadly;

namespace {

void send(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons,
          Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, mods);
    QCoreApplication::sendEvent(w, &ev);
}

void drag(QWidget *w, QPointF from, QPointF to, Qt::MouseButton button, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    send(w, QEvent::MouseButtonPress, from, button, button, mods);
    const int steps = 8;
    for(int i = 1; i <= steps; ++i) send(w, QEvent::MouseMove, from + (to - from) * (double(i) / steps), Qt::NoButton, button, mods);
    send(w, QEvent::MouseButtonRelease, to, button, Qt::NoButton, mods);
}

Camera testCamera() {
    Camera c;
    c.viewport = QSize(800, 600);
    c.target = QVector3D(10, 20, 5);
    c.distance = 250;
    c.rotation = Camera::orientationFor(StandardView::Home);
    return c;
}

} // namespace

class ViewportTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;

    Viewport *vp() { return m_window->viewport(); }

private slots:
    void init() {
        m_window = std::make_unique<MainWindow>();
        buildDemoBracket(m_window->document());
        m_window->refresh();
        m_window->resize(1200, 800);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        vp()->setStandardView(StandardView::Home, false);
        QVERIFY(waitForFrames(vp(), 2));
    }

    void cleanup() { m_window.reset(); }

    // --- camera math -----------------------------------------------------------
    void zoomKeepsTheAnchorUnderTheCursor() {
        for(bool ortho : {false, true}) {
            Camera c = testCamera();
            c.orthographic = ortho;
            const QVector3D anchor(30, 40, 0);
            const QPointF before = c.project(anchor);
            c.zoom(1.8f, anchor);
            QVERIFY(QLineF(before, c.project(anchor)).length() < 0.5);
            c.zoom(0.4f, anchor);
            QVERIFY(QLineF(before, c.project(anchor)).length() < 0.5);
        }
    }

    void orbitKeepsThePivotFixed() {
        Camera c = testCamera();
        const QVector3D pivot(0, 0, 0);
        const QPointF before = c.project(pivot);
        c.orbit(35, -20, pivot);
        QVERIFY(QLineF(before, c.project(pivot)).length() < 0.5);
        // Turntable: horizontal motion keeps the world Z axis vertical on screen.
        Camera d = testCamera();
        d.orbit(90, 0, pivot);
        QVERIFY(std::fabs(QVector3D::dotProduct(d.right(), QVector3D(0, 0, 1))) < 1e-4f);
    }

    void panMovesTheAnchorWithTheCursor() {
        Camera c = testCamera();
        const QVector3D anchor = c.target;
        const QPointF before = c.project(anchor);
        c.pan(QPointF(40, -25), anchor);
        const QPointF after = c.project(anchor);
        QVERIFY(QLineF(before + QPointF(40, -25), after).length() < 0.5);
    }

    void standardViews() {
        Camera c;
        c.setView(StandardView::Top);
        QVERIFY(c.forward().z() < -0.999f);
        QVERIFY(c.up().y() > 0.999f);
        c.setView(StandardView::Front);
        QVERIFY(c.forward().y() > 0.999f);
        QVERIFY(c.up().z() > 0.999f);
        c.setView(StandardView::Right);
        QVERIFY(c.forward().x() < -0.999f);
    }

    void rayAndProjectionAgree() {
        for(bool ortho : {false, true}) {
            Camera c = testCamera();
            c.orthographic = ortho;
            QVector3D o, d;
            const QPointF px(123, 456);
            c.ray(px, o, d);
            QVERIFY(QLineF(c.project(o + d * 100.0f), px).length() < 0.5);
        }
    }

    // --- mouse navigation ---------------------------------------------------------
    void middleDragPans() {
        const QVector3D t0 = vp()->camera().target;
        const QQuaternion r0 = vp()->camera().rotation;
        const QPointF c(vp()->width() / 2.0, vp()->height() / 2.0);
        drag(vp(), c, c + QPointF(120, 0), Qt::MiddleButton);
        QVERIFY((vp()->camera().target - t0).length() > 1.0f);
        QVERIFY(qFuzzyCompare(vp()->camera().rotation, r0));
        // Dragging right moves the model right, i.e. the target moves along -right.
        QVERIFY(QVector3D::dotProduct(vp()->camera().target - t0, vp()->camera().right()) < 0);
    }

    void shiftMiddleDragOrbits() {
        const QQuaternion r0 = vp()->camera().rotation;
        const QPointF c(vp()->width() / 2.0, vp()->height() / 2.0);
        drag(vp(), c, c + QPointF(80, 40), Qt::MiddleButton, Qt::ShiftModifier);
        QVERIFY(!qFuzzyCompare(vp()->camera().rotation, r0));
    }

    void wheelZoomsTowardsTheCursor() {
        const float d0 = vp()->camera().distance;
        const QPointF pos(vp()->width() * 0.3, vp()->height() * 0.4);
        const QVector3D anchor = [&] {
            QVector3D o, d;
            Camera c = vp()->camera();
            c.viewport = vp()->size();
            c.ray(pos, o, d);
            const QVector3D n = c.forward();
            const float t = QVector3D::dotProduct(c.target - o, n) / QVector3D::dotProduct(d, n);
            return vp()->raycast(pos).value_or(o + d * t);
        }();
        const QPointF before = vp()->camera().project(anchor);
        QWheelEvent wheel(pos, vp()->mapToGlobal(pos), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(vp(), &wheel);
        QVERIFY(vp()->camera().distance < d0);
        QVERIFY(QLineF(before, vp()->camera().project(anchor)).length() < 1.0);
    }

    void viewCubeTopGivesTopView() {
        const QRect cube = ViewCube::rect(vp()->size());
        const QMatrix4x4 m = ViewCube::viewProjection(vp()->camera().rotation);
        const QVector4D clip = m * QVector4D(0, 0, 1, 1);
        const QPointF topPx(cube.left() + (clip.x() / clip.w() + 1) * 0.5 * cube.width(),
                            cube.top() + (1 - clip.y() / clip.w()) * 0.5 * cube.height());
        auto hit = ViewCube::hitTest(topPx, vp()->size(), vp()->camera().rotation);
        QVERIFY(hit.has_value());
        QCOMPARE(*hit, QVector3D(0, 0, 1));
        send(vp(), QEvent::MouseButtonPress, topPx, Qt::LeftButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, topPx, Qt::LeftButton, Qt::NoButton);
        QTRY_VERIFY_WITH_TIMEOUT(vp()->camera().forward().z() < -0.999f, 2000);
    }

    void viewCubeCornerGivesIsoView() {
        const QQuaternion q = ViewCube::orientationForRegion(QVector3D(1, -1, 1));
        const QVector3D f = q.rotatedVector(QVector3D(0, 0, -1));
        QVERIFY((f - QVector3D(-1, 1, -1).normalized()).length() < 1e-4f);
    }

    void viewCubePicksFacesBevelledEdgesAndCorners() {
        vp()->setStandardView(StandardView::Home, false);
        const QQuaternion rot = vp()->camera().rotation;
        const QRect cube = ViewCube::rect(vp()->size());
        const QMatrix4x4 m = ViewCube::viewProjection(rot);
        auto px = [&](QVector3D p) {
            const QVector3D ndc = m.map(p);
            return QPointF(cube.left() + (ndc.x() + 1) * 0.5 * cube.width(),
                           cube.top() + (1 - ndc.y()) * 0.5 * cube.height());
        };
        // Points on the chamfered surface: face centre, the middle of the
        // FRONT/RIGHT bevel, the FRONT/RIGHT/TOP corner triangle.
        QCOMPARE(ViewCube::hitTest(px({0, -1, 0}), vp()->size(), rot).value_or(QVector3D()), QVector3D(0, -1, 0));
        QCOMPARE(ViewCube::hitTest(px({0.89f, -0.89f, 0}), vp()->size(), rot).value_or(QVector3D()),
                 QVector3D(1, -1, 0));
        QCOMPARE(ViewCube::hitTest(px({0.93f, -0.93f, 0.93f}), vp()->size(), rot).value_or(QVector3D()),
                 QVector3D(1, -1, 1));
        // Beside the cube: nothing.
        QVERIFY(!ViewCube::hitTest(QPointF(cube.left() + 2, cube.top() + 2), vp()->size(), rot));
    }

    void viewCubeArrowsTurnToTheNeighbouringFace() {
        vp()->setStandardView(StandardView::Home, false);
        QVERIFY(!vp()->cubeFaceOn());
        QCOMPARE(int(vp()->cubeControlAt(vp()->cubeControlShape(Viewport::CubeControl::Up).boundingRect().center())),
                 int(Viewport::CubeControl::None));
        vp()->setStandardView(StandardView::Front, false);
        QVERIFY(vp()->cubeFaceOn());
        auto click = [&](Viewport::CubeControl c) {
            const QPointF at = vp()->cubeControlShape(c).boundingRect().center();
            QCOMPARE(int(vp()->cubeControlAt(at)), int(c));
            send(vp(), QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
            send(vp(), QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
        };
        // FRONT, then the arrow above: looking down at TOP.
        click(Viewport::CubeControl::Up);
        QTRY_VERIFY_WITH_TIMEOUT(vp()->camera().forward().z() < -0.9999f, 2000);
        vp()->setStandardView(StandardView::Front, false);
        // The arrow on the right: looking at RIGHT (along -X).
        click(Viewport::CubeControl::Right);
        QTRY_VERIFY_WITH_TIMEOUT(vp()->camera().forward().x() < -0.9999f, 2000);
    }

    void viewCubeRollArrowsRollTheView() {
        vp()->setStandardView(StandardView::Front, false);
        const QVector3D forward = vp()->camera().forward();
        const QVector3D up0 = vp()->camera().rotation.rotatedVector(QVector3D(0, 1, 0));
        const QPointF at = vp()->cubeControlShape(Viewport::CubeControl::RollCw).boundingRect().center();
        QCOMPARE(int(vp()->cubeControlAt(at)), int(Viewport::CubeControl::RollCw));
        vp()->pressCubeControl(Viewport::CubeControl::RollCw);
        QTRY_VERIFY_WITH_TIMEOUT(
            std::fabs(QVector3D::dotProduct(vp()->camera().rotation.rotatedVector(QVector3D(0, 1, 0)), up0)) < 1e-4f,
            2000);
        QVERIFY((vp()->camera().forward() - forward).length() < 1e-4f);
        QVERIFY(vp()->cubeFaceOn());
    }

    // --- picking and selection ------------------------------------------------------
    void hoverFindsTheFaceUnderTheCursor() {
        const QPointF px = vp()->camera().project(QVector3D(45, 25.5f, 22)); // boss top ring
        send(vp(), QEvent::MouseMove, px, Qt::NoButton, Qt::NoButton);
        const PickHit h = vp()->hovered();
        QCOMPARE(int(h.kind), int(PickHit::Kind::Face));
        const cad::Body *b = m_window->modelView()->state()->body(h.body);
        QVERIFY(b);
        const std::string name = b->shape.faceName(h.index);
        // The boss top is the "end" cap of the boss extrude (feature 4).
        QVERIFY2(name.rfind("f4/end/", 0) == 0, name.c_str());
    }

    void hoverPrefersEdgesNearTheCursor() {
        // A point on the plate's front top edge (y = 0, z = 12).
        const QPointF px = vp()->camera().project(QVector3D(30, 0, 12)) + QPointF(0, 2);
        const PickHit h = vp()->pickAt(px);
        QCOMPARE(int(h.kind), int(PickHit::Kind::Edge));
    }

    void clickSelectsAndShowsStats() {
        const QPointF px = vp()->camera().project(QVector3D(30, 0, 12));
        send(vp(), QEvent::MouseButtonPress, px, Qt::LeftButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, px, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(m_window->modelView()->selection().size(), size_t(1));
        const QString stats = m_window->selectionStatsLabel()->text();
        QVERIFY2(stats.contains(QStringLiteral("Length: 54.00 mm")), qPrintable(stats)); // 60 minus the 6 mm fillet
        // Shift-click adds a second edge; the stats show the total.
        const QPointF px2 = vp()->camera().project(QVector3D(0, 20, 12));
        send(vp(), QEvent::MouseButtonPress, px2, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
        send(vp(), QEvent::MouseButtonRelease, px2, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
        QCOMPARE(m_window->modelView()->selection().size(), size_t(2));
        QVERIFY2(m_window->selectionStatsLabel()->text().contains(QStringLiteral("Total length: 94.00 mm")),
                 qPrintable(m_window->selectionStatsLabel()->text()));
        // Clicking empty space clears the selection.
        send(vp(), QEvent::MouseButtonPress, QPointF(20, vp()->height() - 20), Qt::LeftButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, QPointF(20, vp()->height() - 20), Qt::LeftButton, Qt::NoButton);
        QVERIFY(m_window->modelView()->selection().empty());
    }

    void windowAndCrossingSelectBodies() {
        const QRectF all(QPointF(5, 5), QPointF(vp()->width() - 5, vp()->height() - 5));
        drag(vp(), all.topLeft(), all.bottomRight(), Qt::LeftButton);
        QCOMPARE(m_window->modelView()->selection().count(SelectionItem::Kind::Body), size_t(1));
        QVERIFY(m_window->selectionStatsLabel()->text().contains(QStringLiteral("Volume")));

        m_window->modelView()->clearSelection();
        // A small window over the part selects nothing; a crossing one selects the body.
        const QPointF c = vp()->camera().project(QVector3D(30, 20, 12));
        drag(vp(), c - QPointF(10, 10), c + QPointF(10, 10), Qt::LeftButton);
        QVERIFY(m_window->modelView()->selection().empty());
        drag(vp(), c + QPointF(10, 10), c - QPointF(10, 10), Qt::LeftButton);
        QCOMPARE(m_window->modelView()->selection().count(SelectionItem::Kind::Body), size_t(1));
    }

    void displayStylesRender() {
        for(DisplayStyle s : {DisplayStyle::Shaded, DisplayStyle::Wireframe, DisplayStyle::ShadedWithEdges}) {
            vp()->setDisplayStyle(s);
            QVERIFY(waitForFrames(vp(), 1));
        }
        const QImage img = vp()->grabFramebuffer();
        QVERIFY(!img.isNull());
    }
};

CADLY_REGISTER_TEST(ViewportTests)

#include "tst_viewport.moc"
