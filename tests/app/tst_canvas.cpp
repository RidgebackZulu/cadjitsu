// Canvases: inserting a picture on a plane, seeing it in the viewport,
// calibrating it by clicking two marks, correcting its perspective, undo.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "command/CanvasCommands.h"
#include "command/CommandPanel.h"
#include "image/LensModel.h"
#include "model/CanvasPicture.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "sketch/SketchText.h"
#include "sketch/SketchTools.h"
#include "viewport/Viewport.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace cadjitsu;

namespace {

void send(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

// A 400 x 300 photo: white, a dark block in the middle, and two red marks 200 px apart.
QImage testPhoto() {
    QImage img(400, 300, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.fillRect(QRect(150, 100, 100, 100), QColor(20, 20, 30));
    p.fillRect(QRect(97, 247, 6, 6), Qt::red);
    p.fillRect(QRect(297, 247, 6, 6), Qt::red);
    return img;
}

} // namespace

class CanvasTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;
    QTemporaryDir m_dir;

    Viewport *vp() { return m_window->viewport(); }
    cad::Document &doc() { return m_window->document(); }
    CommandPanel *panel() { return m_window->commandPanel(); }
    template <class T> T *command() { return qobject_cast<T *>(m_window->commands()->command()); }
    QPointF at(QVector3D p) { return vp()->camera().project(p); }
    void click(QPointF p) {
        send(vp(), QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
        send(vp(), QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
        send(vp(), QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
    }
    void settle() {
        m_window->waitForModel(30000);
        QVERIFY(waitForFrames(vp(), 2));
    }
    QString savePhoto(const QImage &img, const char *name) {
        const QString path = m_dir.filePath(QString::fromLatin1(name));
        img.save(path);
        return path;
    }
    void topView() {
        vp()->setStandardView(StandardView::Top, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 2));
    }
    // An inserted canvas on XY, 100 mm wide (0.25 mm per pixel).
    int insertTestCanvas() {
        if(!m_window->insertCanvas(savePhoto(testPhoto(), "part.png"))) return 0;
        auto *c = command<CanvasCommand>();
        if(!c) return 0;
        settle();
        panel()->okButton()->click();
        settle();
        return doc().canvases().empty() ? 0 : doc().canvases().back().id;
    }

private slots:
    void init() {
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1200, 800);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QVERIFY(waitForFrames(vp(), 2));
    }
    void cleanup() { m_window.reset(); }

    void insertedPictureShowsOnItsPlane() {
        QVERIFY(m_window->insertCanvas(savePhoto(testPhoto(), "part.png")));
        auto *c = command<CanvasCommand>();
        QVERIFY(c);
        QCOMPARE(c->widthField()->expression(), QStringLiteral("100 mm"));
        settle();
        QVERIFY(panel()->okButton()->isEnabled());
        panel()->okButton()->click();
        settle();
        QCOMPARE(int(doc().canvases().size()), 1);
        const cad::ReferenceImage r = doc().canvases().front();
        QCOMPARE(r.pixelWidth, 400);
        QVERIFY(std::abs(r.mmPerPixel - 0.25) < 1e-9);
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Insert Canvas"));
        // Seen from above: the dark block is in the middle, the white around it.
        topView();
        cad::ReferenceImage solid = r;
        solid.opacity = 1.0;
        doc().updateCanvas(solid, false);
        settle();
        const QImage img = vp()->grabFramebuffer();
        auto pix = [&](QVector3D w) {
            const QPointF s = at(w);
            return img.pixelColor(int(s.x() * img.width() / vp()->width()), int(s.y() * img.height() / vp()->height()));
        };
        const QColor middle = pix({0, 0, 0}), side = pix({-40, 0, 0});
        QVERIFY2(middle.lightness() < 80, qPrintable(middle.name()));
        QVERIFY2(side.lightness() > 200, qPrintable(side.name()));
        // Hiding it hides it.
        doc().setCanvasVisible(r.id, false);
        settle();
        const QImage hidden = vp()->grabFramebuffer();
        const QPointF s = at({0, 0, 0});
        QVERIFY(hidden.pixelColor(int(s.x() * hidden.width() / vp()->width()), int(s.y() * hidden.height() / vp()->height()))
                    .lightness() > 120);
    }

    void calibrateByClickingTwoMarks() {
        const int id = insertTestCanvas();
        QVERIFY(id);
        topView();
        m_window->calibrateCanvas(id);
        auto *cal = command<CanvasCalibrateCommand>();
        QVERIFY(cal);
        settle();
        QVERIFY(!panel()->okButton()->isEnabled());
        // The two red marks, clicked on screen.
        const cad::ReferenceImage r = *doc().canvas(id);
        for(const cad::Vec2 px : {cad::Vec2(100, 250), cad::Vec2(300, 250)}) {
            const cad::Vec2 p = r.toPlane(px);
            click(at(QVector3D(float(p.x), float(p.y), 0)));
        }
        QCOMPARE(int(cal->points().points.size()), 2);
        // They measure 50 mm as placed; really they are 80 mm apart.
        QCOMPARE(cal->distanceField()->expression(), QStringLiteral("50 mm"));
        cal->distanceField()->setExpression(QStringLiteral("80 mm"));
        settle();
        QVERIFY(panel()->okButton()->isEnabled());
        panel()->okButton()->click();
        settle();
        const cad::ReferenceImage after = *doc().canvas(id);
        QVERIFY2(std::abs(after.mmPerPixel - 0.4) < 2e-3, qPrintable(QString::number(after.mmPerPixel)));
        QVERIFY(std::abs(cad::distance(after.toPlane({100, 250}), after.toPlane({300, 250})) - 80) < 0.5);
        // The first mark stayed where it was.
        QVERIFY(cad::distance(after.toPlane({100, 250}), r.toPlane({100, 250})) < 0.5);
        QVERIFY(QString::fromStdString(doc().undoLabel()).startsWith(QStringLiteral("Calibrate")));
        QVERIFY(doc().undo());
        QVERIFY(std::abs(doc().canvas(id)->mmPerPixel - 0.25) < 1e-9);
    }

    void perspectiveMakesATrapezoidSquare() {
        // A sheet photographed at an angle: a trapezoid, 297 x 210 in reality.
        QImage img(600, 400, QImage::Format_ARGB32);
        img.fill(QColor(90, 90, 90));
        const QPolygonF quad({QPointF(180, 80), QPointF(420, 80), QPointF(560, 330), QPointF(40, 330)});
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(Qt::white);
            p.setPen(Qt::NoPen);
            p.drawPolygon(quad);
        }
        double mm = 0;
        const std::array<cad::Vec2, 4> corners = orderCorners(
            {cad::Vec2(560, 330), cad::Vec2(180, 80), cad::Vec2(40, 330), cad::Vec2(420, 80)});
        QVERIFY(cad::distance(corners[0], {180, 80}) < 1e-9 && cad::distance(corners[2], {560, 330}) < 1e-9);
        const QImage out = correctPerspective(img, corners, 297, 210, mm);
        QVERIFY(!out.isNull());
        // The white sheet's extent in the corrected picture: 297 x 210 mm.
        int x0 = out.width(), x1 = -1, y0 = out.height(), y1 = -1;
        for(int y = 0; y < out.height(); ++y)
            for(int x = 0; x < out.width(); ++x)
                if(out.pixelColor(x, y).lightness() > 230 && out.pixelColor(x, y).alpha() > 200) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
        QVERIFY(x1 > x0 && y1 > y0);
        QVERIFY2(std::abs((x1 - x0 + 1) * mm - 297) < 4, qPrintable(QString::number((x1 - x0 + 1) * mm)));
        QVERIFY2(std::abs((y1 - y0 + 1) * mm - 210) < 4, qPrintable(QString::number((y1 - y0 + 1) * mm)));

        // Through the command: click the four corners on a canvas of that photo.
        QVERIFY(m_window->insertCanvas(savePhoto(img, "sheet.png")));
        settle();
        panel()->okButton()->click();
        settle();
        const int id = doc().canvases().back().id;
        topView();
        m_window->correctCanvasPerspective(id);
        auto *cmd = command<CanvasCalibrateCommand>();
        QVERIFY(cmd);
        settle();
        const cad::ReferenceImage r = *doc().canvas(id);
        for(const QPointF &c : quad) {
            const cad::Vec2 p = r.toPlane({c.x(), c.y()});
            cmd->addPoint(p);
        }
        cmd->widthField()->setExpression(QStringLiteral("297 mm"));
        cmd->heightField()->setExpression(QStringLiteral("210 mm"));
        settle();
        QVERIFY(panel()->okButton()->isEnabled());
        panel()->okButton()->click();
        settle();
        const cad::ReferenceImage after = *doc().canvas(id);
        QVERIFY(after.perspective.has_value());
        QVERIFY(std::abs(after.perspective->realWidth - 297) < 1e-9);
        QVERIFY(cad::distance(after.perspective->corners[0], {180, 80}) < 1.0);
        const QImage shown = canvasPicture(doc(), after);
        QCOMPARE(shown.width(), after.pixelWidth);
        // The sheet now lies square and centred on the canvas's middle: white
        // up to 148.5 mm either side, the grey table beyond.
        auto lightAt = [&](cad::Vec2 plane) {
            const cad::Vec2 px = after.toPixel(plane);
            return shown.pixelColor(int(px.x), int(px.y)).lightness();
        };
        for(const cad::Vec2 p : {cad::Vec2(0, 0), cad::Vec2(140, 0), cad::Vec2(-140, 0), cad::Vec2(0, 98), cad::Vec2(0, -98)})
            QVERIFY2(lightAt(p) > 230, qPrintable(QStringLiteral("%1, %2").arg(p.x).arg(p.y)));
        for(const cad::Vec2 p : {cad::Vec2(158, 0), cad::Vec2(-158, 0), cad::Vec2(0, 115)})
            QVERIFY2(lightAt(p) < 150, qPrintable(QStringLiteral("%1, %2").arg(p.x).arg(p.y)));
        // Save and load: the picture and its correction come back.
        const cad::json file = doc().toJson();
        cad::Document back;
        std::string err;
        QVERIFY(back.fromJson(file, err));
        QVERIFY(back.canvas(id) && back.canvas(id)->perspective);
        QVERIFY(!canvasPicture(back, *back.canvas(id)).isNull());
    }

    void lensCorrectionStraightensBentEdges() {
        // A photo through a barrel lens of a dark frame whose edges are
        // straight in reality (a 600 x 400 photo).
        const cad::LensDistortion barrel{-0.22, 0.0};
        const int w = 600, h = 400;
        QImage img(w, h, QImage::Format_ARGB32);
        for(int y = 0; y < h; ++y)
            for(int x = 0; x < w; ++x) {
                const cad::Vec2 real = cad::undistortPixel(barrel, {x + 0.5, y + 0.5}, w, h);
                const bool frame = real.x > 40 && real.x < 560 && real.y > 30 && real.y < 370 &&
                                   !(real.x > 60 && real.x < 540 && real.y > 50 && real.y < 350);
                img.setPixelColor(x, y, frame ? QColor(30, 30, 40) : QColor(240, 240, 235));
            }
        QVERIFY(m_window->insertCanvas(savePhoto(img, "lens.png")));
        settle();
        panel()->okButton()->click();
        settle();
        const int id = doc().canvases().back().id;
        topView();
        m_window->correctCanvasLens(id);
        auto *cmd = command<CanvasCalibrateCommand>();
        QVERIFY(cmd);
        settle();
        QVERIFY(!panel()->okButton()->isEnabled());
        // Click along the frame's outer top, left and bottom edges, as seen.
        const cad::ReferenceImage r = *doc().canvas(id);
        auto seen = [&](double x, double y) { return r.toPlane(cad::distortPixel(barrel, {x, y}, w, h)); };
        for(int i = 0; i <= 6; ++i) cmd->addPoint(seen(40 + 520.0 * i / 6, 30));
        cmd->nextLine();
        for(int i = 0; i <= 5; ++i) cmd->addPoint(seen(40, 30 + 340.0 * i / 5));
        cmd->nextLine();
        for(int i = 0; i <= 6; ++i) cmd->addPoint(seen(40 + 520.0 * i / 6, 370));
        QCOMPARE(int(cmd->lensLines().size()), 3);
        cmd->previewCheck()->setChecked(true);
        settle();
        QVERIFY(panel()->okButton()->isEnabled());
        panel()->okButton()->click();
        settle();
        const cad::ReferenceImage after = *doc().canvas(id);
        QVERIFY(after.lens.has_value());
        QVERIFY2(std::abs(after.lens->distortion.k1 - barrel.k1) < 0.03, qPrintable(QString::number(after.lens->distortion.k1)));
        QCOMPARE(int(after.lens->lines.size()), 3);
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Correct Lens of Canvas1"));
        // The frame's top edge is straight again: dark at y = 31..49 right
        // across, where the photo shows it bowed.
        const QImage shown = canvasPicture(doc(), after);
        auto dark = [&](const QImage &im, int x, int y) { return im.pixelColor(x, y).lightness() < 128; };
        QVERIFY(!dark(img, 50, 40) || !dark(img, 300, 40) || !dark(img, 550, 40)); // bowed in the photo
        for(int x : {50, 150, 300, 450, 550}) QVERIFY2(dark(shown, x, 40), qPrintable(QString::number(x)));
        // A perspective on top keeps working, and the lens can go again.
        m_window->correctCanvasPerspective(id);
        auto *persp = command<CanvasCalibrateCommand>();
        QVERIFY(persp);
        settle();
        const cad::ReferenceImage src = *doc().canvas(id);
        for(const cad::Vec2 px : {cad::Vec2(40, 30), cad::Vec2(560, 30), cad::Vec2(560, 370), cad::Vec2(40, 370)})
            persp->addPoint(src.toPlane(px));
        persp->widthField()->setExpression(QStringLiteral("260 mm"));
        persp->heightField()->setExpression(QStringLiteral("170 mm"));
        settle();
        panel()->okButton()->click();
        settle();
        cad::ReferenceImage both = *doc().canvas(id);
        QVERIFY(both.perspective && both.lens);
        QVERIFY(setCanvasLens(doc(), both, std::nullopt));
        QVERIFY(!both.lens && both.perspective);
        // The corner clicked at (560, 30) of the corrected photo is that spot of the photo as taken.
        const cad::Vec2 raw = cad::distortPixel(after.lens->distortion, {560, 30}, w, h);
        bool found = false;
        for(const cad::Vec2 &c : both.perspective->corners) found |= cad::distance(c, raw) < 1e-6;
        QVERIFY(found);
        QVERIFY(!canvasPicture(doc(), both).isNull());
    }

    void insertViewsPanelLinesUpTheViews() {
        // A 50 x 20 x 30 block: front (X 50 across, Z 30 up) and top (X 50, Y 20).
        auto picture = [&](const char *name, QRect part) {
            QImage img(300, 240, QImage::Format_ARGB32);
            img.fill(QColor(240, 238, 230));
            QPainter p(&img);
            p.fillRect(part, QColor(40, 50, 70));
            return savePhoto(img, name);
        };
        QAction *a = m_window->action(QStringLiteral("insertViews"));
        QVERIFY(a && a->isEnabled());
        a->trigger();
        auto *cmd = command<InsertViewsCommand>();
        QVERIFY(cmd);
        settle();
        QVERIFY(!panel()->okButton()->isEnabled()); // no pictures yet
        QVERIFY(cmd->setView(cad::ViewSide::Front, picture("front.png", QRect(40, 60, 200, 120))));
        QVERIFY(cmd->setView(cad::ViewSide::Top, picture("top.png", QRect(80, 100, 100, 40))));
        QVERIFY(!cmd->setView(cad::ViewSide::Side, m_dir.filePath(QStringLiteral("missing.png"))));
        cmd->axisChoice()->setCurrentIndex(0);
        cmd->sizeField()->setExpression(QStringLiteral("50 mm"));
        settle();
        QVERIFY2(panel()->okButton()->isEnabled(), qPrintable(cmd->info()->text()));
        QVERIFY2(cmd->info()->text().contains(QStringLiteral("20")), qPrintable(cmd->info()->text()));
        panel()->okButton()->click();
        settle();
        QCOMPARE(int(doc().canvases().size()), 2);
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Insert Views"));
        const cad::ReferenceImage front = doc().canvases()[0], top = doc().canvases()[1];
        QCOMPARE(front.plane.kind, cad::PlaneRef::Kind::XZ);
        QCOMPARE(top.plane.kind, cad::PlaneRef::Kind::XY);
        // Front: 200 px is 50 mm; the block's bottom-left corner at x -25, z 0.
        QVERIFY(std::abs(front.mmPerPixel - 0.25) < 1e-6);
        QVERIFY(cad::distance(front.toPlane({40, 180}), {-25, 0}) < 0.3);
        // Top: 100 px is 50 mm; centred on the origin.
        QVERIFY(std::abs(top.mmPerPixel - 0.5) < 1e-6);
        QVERIFY(cad::distance(top.toPlane({130, 120}), {0, 0}) < 0.3);
        doc().undo();
        QVERIFY(doc().canvases().empty());
    }

    void traceAPlateIntoTheSketch() {
        // A dark plate 60 x 40 mm with 10 mm round corners and a 12 mm hole,
        // on a light table: 0.25 mm per pixel once inserted 100 mm wide.
        QImage img(400, 300, QImage::Format_ARGB32);
        img.fill(QColor(235, 232, 225));
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(40, 45, 60));
            p.drawRoundedRect(QRectF(80, 70, 240, 160), 40, 40);
            p.setBrush(QColor(235, 232, 225));
            p.drawEllipse(QPointF(260, 150), 24, 24);
        }
        QVERIFY(m_window->insertCanvas(savePhoto(img, "plate.png")));
        settle();
        panel()->okButton()->click();
        settle();
        SketchMode *sm = m_window->sketchMode();
        QVERIFY(sm->beginNewSketch(cad::PlaneRef::origin(cad::PlaneRef::Kind::XY), false));
        topView();
        QAction *a = m_window->action(QStringLiteral("sketchTrace"));
        QVERIFY(a && a->isEnabled());
        a->trigger();
        QCOMPARE(sm->tool(), SketchToolKind::TraceCanvas);
        // Inside the plate, left of the hole: (-15, 0) mm from the middle.
        click(at({-15, 0, 0}));
        settle();
        const cad::Sketch &sk = sm->editor()->sketch();
        int lines = 0, arcs = 0, circles = 0;
        for(const auto &e : sk.entities) {
            lines += e.type == cad::SkType::Line;
            arcs += e.type == cad::SkType::Arc;
            circles += e.type == cad::SkType::Circle;
        }
        QVERIFY2(lines == 4 && arcs == 4 && circles == 1,
                 qPrintable(QStringLiteral("%1 lines, %2 arcs, %3 circles").arg(lines).arg(arcs).arg(circles)));
        // Two profiles: the plate less its hole (at the photo's scale), and the hole.
        const auto profiles = cad::sketchProfiles(sk);
        QCOMPARE(int(profiles.size()), 2);
        double big = 0;
        for(const auto &pr : profiles) big = std::max(big, std::abs(pr.area));
        const double area = 60 * 40 - 100 * (4 - cad::kPi) - cad::kPi * 36;
        QVERIFY2(std::abs(big - area) < 0.04 * area,
                 qPrintable(QString::number(big)));
    }
};

CADJITSU_REGISTER_TEST(CanvasTests)

#include "tst_canvas.moc"
