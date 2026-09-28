// Section analysis: cutting the model with the command, hatched caps, the
// browser eye, dragging the depth arrow, modelling while sectioned, and the
// section following the model.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "command/Command.h"
#include "command/CanvasValueBox.h"
#include "command/CommandPanel.h"
#include "command/EdgeCommands.h"
#include "command/Manipulator.h"
#include "command/SectionCommand.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "ui/BrowserTree.h"
#include "viewport/Viewport.h"

#include "features/ExtrudeFeature.h"
#include "features/FilletFeature.h"
#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"
#include "topo/Resolver.h"

#include <gp_Pln.hxx>

#include <QAction>
#include <QCheckBox>
#include <QPushButton>
#include <QtTest>

#include <cmath>

using namespace cadjitsu;

namespace {

void send(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

void doubleClick(QWidget *w, QPoint pos) {
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseDClick(w, Qt::LeftButton, Qt::NoModifier, pos);
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, pos);
}

// Which side of the cutting plane a point is on: true if it is cut away.
bool cutAway(const QVector4D &plane, QVector3D p) { return QVector3D::dotProduct(plane.toVector3D(), p) + plane.w() > 0; }

} // namespace

class SectionTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;
    cad::FeatureId m_extrude = cad::kNoFeature;

    Viewport *vp() { return m_window->viewport(); }
    cad::Document &doc() { return m_window->document(); }
    ModelView *view() { return m_window->modelView(); }
    CommandPanel *panel() { return m_window->commandPanel(); }
    template <class T> T *command() { return qobject_cast<T *>(m_window->commands()->command()); }

    QPointF at(double x, double y, double z) { return vp()->camera().project(QVector3D(float(x), float(y), float(z))); }
    void click(QPointF p) {
        send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
        send(vp(), QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    }
    void drag(QPointF from, QPointF to) {
        send(vp(), QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
        send(vp(), QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for(int i = 1; i <= 10; ++i) send(vp(), QEvent::MouseMove, from + (to - from) * (i / 10.0), Qt::NoButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
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
    // A 40 x 20 x 10 box, shown from the default angle.
    void box() {
        auto s = std::make_shared<cad::SketchFeature>();
        s->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        s->sketch.addRectangle({0, 0}, {40, 20});
        const cad::FeatureId sid = doc().addFeature(s);
        auto e = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &p : doc().stateAt(doc().marker())->sketches.at(sid)->profiles) e->profiles.push_back({sid, p.key, p.sample});
        e->distance = doc().makeSlot("10 mm");
        m_extrude = doc().addFeature(e);
        m_window->refresh();
        vp()->setStandardView(StandardView::Home, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 1));
    }
    // The section through the middle of the box (y = 10), keeping the back half.
    void sectionAcrossTheMiddle() {
        cad::SectionAnalysis s;
        s.plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XZ); // normal -Y
        s.offset = -10;
        doc().addSection(s);
        settle();
    }
    // Brightest and darkest pixels in a short horizontal strip of the canvas.
    std::pair<int, int> stripRange(const QImage &img, QPointF p) {
        int lo = 255, hi = 0;
        const QPoint c(int(p.x() * img.width() / vp()->width()), int(p.y() * img.height() / vp()->height()));
        for(int dx = -8; dx <= 8; ++dx) {
            const int v = qGray(img.pixel(c.x() + dx, c.y()));
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        return {lo, hi};
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

    void theCommandCutsTheModelWithHatchedCaps() {
        box();
        const QImage before = vp()->grabFramebuffer();
        trigger("sectionAnalysis");
        auto *section = command<SectionCommand>();
        QVERIFY(section);
        // The origin planes are there to pick: the XZ plane, beside the box.
        QPointF xz;
        for(const auto &[item, q] : view()->planeQuads())
            if(item.key == "XZ") xz = vp()->camera().project(q[0] + (q[1] - q[0]) * 0.9f + (q[3] - q[0]) * 0.9f);
        click(xz);
        QVERIFY(section->hasPlane());
        typeInto(section->distanceField(), QStringLiteral("-10"));
        settle();
        // Previewed: the front half (y < 10) is cut away.
        QVERIFY(view()->clipPlane().has_value());
        QVERIFY(cutAway(*view()->clipPlane(), QVector3D(20, 5, 5)));
        QVERIFY(!cutAway(*view()->clipPlane(), QVector3D(20, 15, 5)));
        QVERIFY(doc().sections().empty()); // not in the document until OK
        panel()->okButton()->click();
        settle();
        QCOMPARE(int(doc().sections().size()), 1);
        QVERIFY(doc().activeSection() && doc().activeSection()->offset == -10.0);
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Create Section Analysis"));
        QVERIFY(view()->clipPlane().has_value());
        QTreeWidgetItem *analysis = m_window->browser()->folder(QStringLiteral("Analysis"));
        QTRY_VERIFY(analysis && analysis->childCount() == 1);
        // The cut face shows hatching: light cap and dark lines side by side.
        QVERIFY(waitForFrames(vp(), 2));
        const QImage img = vp()->grabFramebuffer();
        const auto [lo, hi] = stripRange(img, at(20, 10, 5));
        QVERIFY2(hi - lo > 40, qPrintable(QStringLiteral("%1..%2").arg(lo).arg(hi)));
        const auto [lo0, hi0] = stripRange(before, at(20, 10, 5));
        QVERIFY2(hi0 - lo0 < 25, qPrintable(QStringLiteral("before: %1..%2").arg(lo0).arg(hi0)));
    }

    void theBrowserEyeTurnsItOffAndOn() {
        box();
        sectionAcrossTheMiddle();
        QVERIFY(view()->clipPlane().has_value());
        BrowserTree *b = m_window->browser();
        QTRY_VERIFY(b->folder(QStringLiteral("Analysis")) && b->folder(QStringLiteral("Analysis"))->childCount() == 1);
        auto eye = [&] {
            QTreeWidgetItem *it = b->folder(QStringLiteral("Analysis"))->child(0);
            return QPoint(b->columnViewportPosition(1) + 10, b->visualItemRect(it).center().y());
        };
        QTest::mouseClick(b->viewport(), Qt::LeftButton, Qt::NoModifier, eye());
        QVERIFY(!doc().activeSection());
        QVERIFY(!view()->clipPlane().has_value()); // back to the normal view at once
        QTRY_VERIFY(b->folder(QStringLiteral("Analysis"))->childCount() == 1);
        QTest::mouseClick(b->viewport(), Qt::LeftButton, Qt::NoModifier, eye());
        QVERIFY(doc().activeSection());
        QVERIFY(view()->clipPlane().has_value());
    }

    void theDepthArrowCanBeDraggedAnyTime() {
        box();
        sectionAcrossTheMiddle();
        DistanceManipulator *arrow = m_window->sectionArrow();
        QVERIFY(arrow->visible());
        QCOMPARE(vp()->activeTool(), static_cast<ViewportTool *>(arrow));
        // Drag the head 4 mm along the plane's normal (-Y), from -10 to -6: the cut moves to y = 6.
        const QPointF head = arrow->headOnScreen();
        const QVector3D target = arrow->origin() + arrow->direction() * float(arrow->distance() + 4.0);
        drag(head, at(target.x(), target.y(), target.z()));
        QVERIFY2(std::fabs(doc().activeSection()->offset + 6.0) < 0.05,
                 qPrintable(QString::number(doc().activeSection()->offset)));
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Edit Section1"));
        QVERIFY(cutAway(*view()->clipPlane(), QVector3D(20, 5, 5)));
        QVERIFY(!cutAway(*view()->clipPlane(), QVector3D(20, 7, 5)));
        m_window->undo();
        QVERIFY(std::fabs(doc().activeSection()->offset + 10.0) < 1e-9);
    }

    // Beside the handle, a value box shows the depth: it follows a drag, and a
    // typed depth (Enter) moves the cut, as one undo step.
    void theDepthBoxFollowsTheHandleAndTakesTypedValues() {
        box();
        sectionAcrossTheMiddle();
        DistanceManipulator *arrow = m_window->sectionArrow();
        CanvasValueBox *depth = m_window->sectionDepthBox();
        QVERIFY(waitForFrames(vp(), 2));
        QTRY_VERIFY(depth->isVisible());
        QCOMPARE(depth->text(), QStringLiteral("-10"));
        // Next to the handle, not over it.
        const QPointF head = arrow->headOnScreen();
        QVERIFY(!depth->geometry().contains(head.toPoint()));
        QVERIFY(QLineF(head, depth->geometry().topLeft()).length() < 60);
        QVERIFY(arrow->nearHead(head));
        // Sliding the handle updates the box while dragging.
        const QVector3D target = arrow->origin() + arrow->direction() * float(arrow->distance() + 3.0);
        const QPointF to = at(target.x(), target.y(), target.z());
        send(vp(), QEvent::MouseMove, head, Qt::NoButton, Qt::NoButton);
        send(vp(), QEvent::MouseButtonPress, head, Qt::LeftButton, Qt::LeftButton);
        for(int i = 1; i <= 10; ++i) send(vp(), QEvent::MouseMove, head + (to - head) * (i / 10.0), Qt::NoButton, Qt::LeftButton);
        QVERIFY2(std::fabs(depth->text().toDouble() + 7.0) < 0.05, qPrintable(depth->text()));
        QVERIFY(std::fabs(doc().activeSection()->offset + 10.0) < 1e-9); // not kept until let go
        send(vp(), QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        QVERIFY(std::fabs(doc().activeSection()->offset + 7.0) < 0.05);
        // Typed: the preview follows as you type; Enter keeps it.
        depth->setFocus();
        depth->selectAll();
        QTest::keyClicks(depth, QStringLiteral("-4"));
        QVERIFY(cutAway(*view()->clipPlane(), QVector3D(20, 3, 5)));
        QVERIFY(!cutAway(*view()->clipPlane(), QVector3D(20, 5, 5)));
        QTest::keyClick(depth, Qt::Key_Return);
        QVERIFY(std::fabs(doc().activeSection()->offset + 4.0) < 1e-9);
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Edit Section1"));
        QVERIFY(std::fabs(arrow->distance() + 4.0) < 1e-9);
        // Esc gives up a typed value.
        depth->setFocus();
        depth->selectAll();
        QTest::keyClicks(depth, QStringLiteral("-8"));
        QTest::keyClick(depth, Qt::Key_Escape);
        QVERIFY(std::fabs(doc().activeSection()->offset + 4.0) < 1e-9);
        QCOMPARE(depth->text(), QStringLiteral("-4"));
        // A command has its own box: this one steps aside.
        trigger("fillet");
        QVERIFY(waitForFrames(vp(), 2));
        QVERIFY(!depth->isVisible());
        panel()->cancelButton()->click();
    }

    void modellingGoesOnWhileSectioned() {
        box();
        sectionAcrossTheMiddle();
        trigger("fillet");
        auto *fillet = command<FilletCommand>();
        QVERIFY(fillet);
        // The front top edge is cut away: nothing to pick there.
        click(at(20, 0, 10));
        QCOMPARE(fillet->edgeCount(), 0);
        // The back top edge is still there.
        click(at(20, 20, 10));
        QCOMPARE(fillet->edgeCount(), 1);
        typeInto(fillet->radiusField(), QStringLiteral("2"));
        settle();
        panel()->okButton()->click();
        settle();
        QCOMPARE(doc().statusOf(doc().features().back()->id).severity, cad::Severity::Ok);
        QVERIFY(std::dynamic_pointer_cast<const cad::FilletFeature>(doc().features().back()));
        QVERIFY(view()->clipPlane().has_value()); // still sectioned
    }

    void aSectionOnAFaceFollowsTheModel() {
        box();
        // Section on the top face, 3 mm down: cuts at z = 7.
        const cad::Body *b = view()->state()->bodies.begin()->second.get();
        int top = 0;
        for(int i = 1; i <= b->shape.faceCount(); ++i) {
            gp_Pln pln;
            if(cad::planeOfFace(b->shape.face(i), pln) && pln.Axis().Direction().IsEqual(gp_Dir(0, 0, 1), 1e-9)) top = i;
        }
        cad::SectionAnalysis s;
        s.plane = cad::PlaneRef::onFace(cad::makeTopoRef(*b, cad::TopoKind::Face, top));
        s.offset = -3;
        doc().addSection(s);
        settle();
        QVERIFY(cutAway(*view()->clipPlane(), QVector3D(20, 10, 8)));
        QVERIFY(!cutAway(*view()->clipPlane(), QVector3D(20, 10, 6)));
        // The box gets taller: the cut goes up with its top face (z = 17).
        auto e = std::static_pointer_cast<cad::ExtrudeFeature>(doc().feature(m_extrude)->clone());
        e->distance.expr = "20 mm";
        doc().replaceFeature(e);
        settle();
        QVERIFY(cutAway(*view()->clipPlane(), QVector3D(20, 10, 18)));
        QVERIFY(!cutAway(*view()->clipPlane(), QVector3D(20, 10, 16)));
    }

    void editingASectionFromTheBrowser() {
        box();
        sectionAcrossTheMiddle();
        BrowserTree *b = m_window->browser();
        QTRY_VERIFY(b->folder(QStringLiteral("Analysis")) && b->folder(QStringLiteral("Analysis"))->childCount() == 1);
        QTreeWidgetItem *it = b->folder(QStringLiteral("Analysis"))->child(0);
        doubleClick(b->viewport(), b->visualItemRect(it).center());
        auto *section = command<SectionCommand>();
        QVERIFY(section && section->hasPlane());
        QCOMPARE(section->distanceField()->value().value_or(0.0), -10.0);
        section->flipBox()->setChecked(true);
        settle();
        QVERIFY(!cutAway(*view()->clipPlane(), QVector3D(20, 5, 5))); // the other half now
        panel()->okButton()->click();
        settle();
        QVERIFY(doc().activeSection()->flip);
        QCOMPARE(int(doc().sections().size()), 1);
        // Cancel leaves it as it was.
        m_window->editSection(doc().activeSection()->id);
        command<SectionCommand>()->flipBox()->setChecked(false);
        panel()->cancelButton()->click();
        settle();
        QVERIFY(doc().activeSection()->flip);
        QVERIFY(!cutAway(*view()->clipPlane(), QVector3D(20, 5, 5)));
    }
};

CADJITSU_REGISTER_TEST(SectionTests)

#include "tst_section.moc"
