// The Render dialog: its menus change the bodies' materials and the render
// settings at once (one undo step per change), the canvas shows the Rendered
// style while it is open, and a body's "Material..." opens it on that body.
// The Rendered style: a material changes how the body looks, a semitransparent
// body shows what is behind it, and the plate's surface follows the menu.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "mcp/McpTools.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "ui/BrowserTree.h"
#include "ui/RenderDialog.h"
#include "viewport/Viewport.h"


#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

using namespace cadjitsu;

class RenderTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;

    Viewport *vp() { return m_window->viewport(); }
    cad::Document &doc() { return m_window->document(); }

    // A box (Body1) and, behind it as seen from the front, a taller wall (Body2).
    void build() {
        using json = nlohmann::json;
        McpTools &t = *m_window->mcpTools();
        const json rects = {{{"type", "rectangle"}, {"corner1", {-15, -15}}, {"corner2", {15, 0}}},
                            {{"type", "rectangle"}, {"corner1", {-25, 20}}, {"corner2", {25, 26}}}};
        const json sk = t.callTool("create_sketch", {{"plane", "XY"}, {"entities", rects}});
        QVERIFY2(!sk.value("isError", false), sk.dump().c_str());
        const json ex = t.callTool("extrude", {{"sketch", 1}, {"distance", 30}});
        QVERIFY2(!ex.value("isError", false), ex.dump().c_str());
        m_window->refresh();
        QVERIFY(m_window->waitForModel());
        QCOMPARE(int(m_window->modelView()->state()->orderedBodies().size()), 2);
        cad::RenderSettings rs = doc().renderSettings();
        rs.rayTraced = false; // the live preview, so the frames are repeatable
        doc().setRenderSettings(rs);
        vp()->setStandardView(StandardView::Front, false);
        vp()->fitAll(false);
        QVERIFY(waitForFrames(vp(), 2));
    }

    cad::BodyId bodyAt(int index) { return m_window->modelView()->state()->orderedBodies()[size_t(index)]->id; }

    QColor at(const QVector3D &p) {
        waitForFrames(vp(), 2);
        const QImage img = vp()->grabFramebuffer();
        const QPointF s = vp()->camera().project(p);
        return img.pixelColor(int(s.x() * img.width() / vp()->width()), int(s.y() * img.height() / vp()->height()));
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

    void theMenusChangeMaterialsAndSettingsLive() {
        build();
        RenderDialog *dlg = m_window->openRenderDialog();
        QVERIFY(dlg->isVisible());
        QCOMPARE(vp()->displayStyle(), DisplayStyle::Rendered);
        QCOMPARE(dlg->bodyList()->count(), 2);
        QCOMPARE(int(dlg->bodyList()->selectedItems().size()), 2); // all bodies at first

        // Only the first body: PETG, semitransparent, a blue from the swatches.
        dlg->selectBodies({bodyAt(0)});
        dlg->materialButton(cad::PrintMaterial::PETG)->click();
        QCOMPARE(doc().bodyMaterial(bodyAt(0)).material, cad::PrintMaterial::PETG);
        QCOMPARE(doc().bodyMaterial(bodyAt(1)), cad::defaultBodyMaterial());
        dlg->finishButton(cad::Finish::Translucent)->click();
        QCOMPARE(doc().bodyMaterial(bodyAt(0)).finish, cad::Finish::Translucent);
        QToolButton *blue = nullptr;
        for(QToolButton *b : dlg->colorButtons())
            if(b->toolTip() == QStringLiteral("Ice Blue")) blue = b;
        QVERIFY(blue);
        blue->click();
        QCOMPARE(QString::fromStdString(doc().bodyMaterial(bodyAt(0)).colorName), QStringLiteral("Ice Blue"));
        QCOMPARE(QString::fromStdString(doc().undoLabel()), QStringLiteral("Body Material"));
        QVERIFY(dlg->bodyList()->item(0)->text().contains(QStringLiteral("PETG")));

        // Render settings: no undo step, straight into the document.
        dlg->plateButton(cad::BuildPlateKind::SmoothPEI)->click();
        QCOMPARE(doc().renderSettings().plate, cad::BuildPlateKind::SmoothPEI);
        dlg->lightingButton(cad::Lighting::Daylight)->click();
        QCOMPARE(doc().renderSettings().lighting, cad::Lighting::Daylight);
        dlg->layerHeightBox()->setValue(0.12);
        QCOMPARE(doc().renderSettings().layerHeight, 0.12);
        dlg->layerLinesBox()->setChecked(false);
        QVERIFY(!doc().renderSettings().layerLines);
        dlg->placementBox()->setCurrentIndex(1);
        emit dlg->placementBox()->activated(1);
        QCOMPARE(doc().renderSettings().placement, cad::Placement::AsModelled);
        dlg->modeButton(true)->click();
        QVERIFY(doc().renderSettings().rayTraced);
        QVERIFY(!dlg->statusLabel()->text().isEmpty());

        // Undo brings back the colour, and the dialog follows.
        m_window->undo();
        QVERIFY(doc().bodyMaterial(bodyAt(0)).colorName != "Ice Blue");
        QTRY_VERIFY(!dlg->bodyList()->item(0)->text().contains(QStringLiteral("Ice Blue")));
    }

    void aBodysMaterialMenuOpensTheDialogOnIt() {
        build();
        emit m_window->browser()->materialRequested({bodyAt(1)});
        QTRY_VERIFY(m_window->findChild<RenderDialog *>());
        RenderDialog *dlg = m_window->findChild<RenderDialog *>();
        QCOMPARE(int(dlg->bodyList()->selectedItems().size()), 1);
        QCOMPARE(dlg->bodyList()->selectedItems().front()->data(Qt::UserRole).toString().toStdString(), bodyAt(1));
    }

    void materialsShowInTheRenderedStyle() {
        build();
        vp()->setDisplayStyle(DisplayStyle::Rendered);
        const QVector3D front(0, -15, 15), wall(20, 20, 15);
        const QColor grey = at(front);
        cad::BodyMaterial red;
        red.rgb = 0xc8102e;
        red.colorName = "Signal Red";
        doc().setBodyMaterial(bodyAt(0), red);
        m_window->refresh();
        const QColor r = at(front);
        QVERIFY2(r.red() > r.green() + 60, qPrintable(r.name()));
        QVERIFY(r != grey);

        // A white wall behind a semitransparent blue box shows through it, tinted.
        cad::BodyMaterial white;
        white.rgb = 0xf2f2f2;
        doc().setBodyMaterial(bodyAt(1), white);
        cad::BodyMaterial clear;
        clear.material = cad::PrintMaterial::PETG;
        clear.finish = cad::Finish::Translucent;
        clear.rgb = 0x7fb8f0;
        doc().setBodyMaterial(bodyAt(0), clear);
        m_window->refresh();
        const QColor through = at(front);
        cad::BodyMaterial opaque = clear;
        opaque.finish = cad::Finish::Matte;
        doc().setBodyMaterial(bodyAt(0), opaque);
        m_window->refresh();
        const QColor solid = at(front);
        QVERIFY2(through.blue() > through.red(), qPrintable(through.name()));
        QVERIFY2(through != solid, qPrintable(through.name() + QLatin1Char(' ') + solid.name()));

        // The plate: textured and smooth PEI look different; none shows no plate.
        vp()->setStandardView(StandardView::Home, false);
        const QVector3D onPlate(-30, -25, 0);
        cad::RenderSettings rs = doc().renderSettings();
        const QColor textured = at(onPlate);
        rs.plate = cad::BuildPlateKind::SmoothPEI;
        doc().setRenderSettings(rs);
        m_window->refresh();
        const QColor smooth = at(onPlate);
        rs.plate = cad::BuildPlateKind::None;
        doc().setRenderSettings(rs);
        m_window->refresh();
        const QColor none = at(onPlate);
        QVERIFY2(textured != smooth, qPrintable(textured.name()));
        QVERIFY2(none != textured, qPrintable(none.name()));
        QVERIFY(qAbs(textured.red() - textured.blue()) > 30); // PEI is golden
        Q_UNUSED(wall);
    }

    void thePathTracerTakesOverWhenTheViewRests() {
        build();
        cad::RenderSettings rs = doc().renderSettings();
        rs.rayTraced = true;
        doc().setRenderSettings(rs);
        vp()->setDisplayStyle(DisplayStyle::Rendered);
        m_window->refresh();
        QTRY_VERIFY_WITH_TIMEOUT(vp()->tracedSamples() >= 2, 60000);
        // Moving away drops it at once.
        vp()->setStandardView(StandardView::Top, false);
        QVERIFY(waitForFrames(vp(), 2));
        QCOMPARE(vp()->tracedSamples(), 0);
        // Other styles never trace.
        vp()->setDisplayStyle(DisplayStyle::Shaded);
        processEventsFor(600);
        QCOMPARE(vp()->tracedSamples(), 0);
        QVERIFY(!vp()->tracing() || vp()->tracedSamples() == 0);
    }
};

CADJITSU_REGISTER_TEST(RenderTests)

#include "tst_render.moc"
