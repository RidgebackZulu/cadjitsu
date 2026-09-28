// File > Export and MAKE > 3D Print: the STL is checked to be printable before
// it is written, the STEP is read back with the same volume, "selected bodies"
// exports only those, and an open command's preview is never exported.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "command/Command.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "ui/AppIcon.h"
#include "ui/ExportDialog.h"
#include "viewport/Viewport.h"

#include "features/ExtrudeFeature.h"
#include "features/SketchFeature.h"
#include "geom/OcctUtil.h"
#include "io/StepIO.h"
#include "io/StlWriter.h"
#include "mesh/MeshValidator.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace cadjitsu;

class ExportTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;
    std::unique_ptr<QTemporaryDir> m_dir;

    cad::Document &doc() { return m_window->document(); }
    ModelView *view() { return m_window->modelView(); }
    QString path(const char *name) { return m_dir->filePath(QString::fromLatin1(name)); }

    // A box made of a rectangle sketch and an extrude, straight into the document.
    void box(double x0, double y0, double x1, double y1, double height) {
        auto s = std::make_shared<cad::SketchFeature>();
        s->plane = cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
        s->sketch.addRectangle({x0, y0}, {x1, y1});
        const cad::FeatureId sid = doc().addFeature(s);
        auto e = std::make_shared<cad::ExtrudeFeature>();
        for(const auto &p : doc().stateAt(doc().marker())->sketches.at(sid)->profiles)
            e->profiles.push_back({sid, p.key, p.sample});
        e->distance = doc().makeSlot(std::to_string(height) + " mm");
        e->operation = cad::BodyOperation::NewBody;
        doc().addFeature(e);
    }

    // Opens the dialog through the menu action, as a user does.
    ExportDialog *open(const char *action) {
        QAction *a = m_window->action(QString::fromLatin1(action));
        if(!a || !a->isEnabled()) return nullptr;
        a->trigger();
        return m_window->findChild<ExportDialog *>(QStringLiteral("exportDialog"));
    }

    // Presses Export (to `file`, without the save dialog) and waits for it.
    bool exportTo(ExportDialog *dlg, const QString &file) {
        dlg->setOutputPath(file);
        dlg->setAskBeforeWritingInvalid(false);
        QSignalSpy finished(dlg, &ExportDialog::exportFinished);
        auto *button = dlg->findChild<QPushButton *>(QStringLiteral("exportButton"));
        if(!button || !button->isEnabled()) return false;
        button->click();
        const bool background = dlg->busy(); // the export runs on a worker thread
        return (finished.count() > 0 || finished.wait(60000)) && background;
    }

private slots:
    void init() {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1300, 850);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QVERIFY(waitForFrames(m_window->viewport(), 2));
    }

    void cleanup() {
        m_window.reset();
        m_dir.reset();
    }

    void printingWritesACheckedWatertightStl() {
        box(0, 0, 40, 20, 10);
        box(30, 0, 50, 20, 10); // a second body overlapping the first
        m_window->refresh();
        QCOMPARE(int(view()->state()->bodies.size()), 2);
        ExportDialog *dlg = open("print3d");
        QVERIFY(dlg);
        QVERIFY(dlg->isVisible());
        QCOMPARE(dlg->format(), ExportJob::Format::Stl);
        QVERIFY(dlg->mergeBox()->isChecked());
        dlg->refinementBox()->setCurrentIndex(2); // fine
        QVERIFY(exportTo(dlg, path("part.stl")));
        const ExportResult &r = dlg->lastResult();
        QVERIFY2(r.ok, r.error.c_str());
        QVERIFY(r.meshChecked);
        QVERIFY2(r.report.ok, r.report.summary().c_str());
        QCOMPARE(int(r.report.shells), 1); // merged into one solid
        QVERIFY(std::fabs(r.solidVolume - 10000.0) < 1e-6);
        QVERIFY(dlg->report().contains(QStringLiteral("watertight")));
        // What was written reads back as the same printable mesh.
        cad::TriMesh mesh;
        std::string error;
        QVERIFY2(cad::readStlFile(path("part.stl").toStdString(), mesh, error), error.c_str());
        const cad::MeshReport back = cad::validateMesh(mesh, 10000.0);
        QVERIFY2(back.ok, back.summary().c_str());
        QCOMPARE(back.triangles, r.report.triangles);
    }

    void stepExportReadsBackTheSameVolume() {
        box(0, 0, 40, 20, 10);
        box(60, 0, 80, 20, 5);
        m_window->refresh();
        ExportDialog *dlg = open("export");
        QVERIFY(dlg);
        QCOMPARE(dlg->format(), ExportJob::Format::Step);
        QVERIFY(exportTo(dlg, path("part.step")));
        const ExportResult &r = dlg->lastResult();
        QVERIFY2(r.ok, r.error.c_str());
        QVERIFY(r.reimported);
        QVERIFY(std::fabs(r.solidVolume - 10000.0) < 1e-6);
        QVERIFY2(std::fabs(r.reimportedVolume - r.solidVolume) < 1e-6 * r.solidVolume,
                 qPrintable(QString::number(r.reimportedVolume)));
        std::vector<cad::NamedSolid> solids;
        std::string error;
        QVERIFY2(cad::readStepFile(path("part.step").toStdString(), solids, error), error.c_str());
        QCOMPARE(int(solids.size()), 2); // each body stays its own solid
        QVERIFY(dlg->report().contains(QStringLiteral("part.step")));
    }

    void selectedBodiesOnly() {
        box(0, 0, 40, 20, 10);
        box(60, 0, 80, 20, 5);
        m_window->refresh();
        const auto bodies = view()->state()->orderedBodies();
        QCOMPARE(int(bodies.size()), 2);
        SelectionSet sel;
        SelectionItem it;
        it.kind = SelectionItem::Kind::Body;
        it.body = bodies[1]->id;
        sel.add(it);
        view()->setSelection(sel);
        ExportDialog *dlg = open("print3d");
        QVERIFY(dlg);
        auto *which = dlg->findChild<QComboBox *>(QStringLiteral("exportBodies"));
        QVERIFY(which);
        QCOMPARE(which->currentIndex(), 1); // "Selected bodies (1)" when something is selected
        QVERIFY(exportTo(dlg, path("one.stl")));
        QVERIFY(dlg->lastResult().ok);
        QVERIFY(std::fabs(dlg->lastResult().solidVolume - 2000.0) < 1e-6);
        // Back to all visible bodies.
        which->setCurrentIndex(0);
        QVERIFY(exportTo(dlg, path("all.stl")));
        QVERIFY(std::fabs(dlg->lastResult().solidVolume - 10000.0) < 1e-6);
    }

    void aCommandsPreviewIsNotExported() {
        box(0, 0, 40, 20, 10);
        m_window->refresh();
        // An extrude being set up changes what the canvas shows...
        QAction *extrude = m_window->action(QStringLiteral("extrude"));
        QVERIFY(extrude && extrude->isEnabled());
        extrude->trigger();
        QVERIFY(m_window->commands()->active());
        // ...exporting ends it and exports the design.
        ExportDialog *dlg = open("print3d");
        QVERIFY(dlg);
        QVERIFY(!m_window->commands()->active());
        QVERIFY(exportTo(dlg, path("design.stl")));
        QVERIFY(std::fabs(dlg->lastResult().solidVolume - 8000.0) < 1e-6);
    }

    void closingTheDialogDeletesIt() {
        box(0, 0, 10, 10, 10);
        m_window->refresh();
        QPointer<ExportDialog> dlg = open("print3d");
        QVERIFY(dlg);
        dlg->reject();
        QTRY_VERIFY(dlg.isNull());
    }

    void openInSlicerIsRemembered() {
        QSettings().remove(QStringLiteral("export/openInSlicer"));
        box(0, 0, 10, 10, 10);
        m_window->refresh();
        QPointer<ExportDialog> dlg = open("print3d");
        QVERIFY(dlg);
        auto *slicer = dlg->findChild<QCheckBox *>(QStringLiteral("exportOpenAfter"));
        QVERIFY(slicer && !slicer->isChecked());
        slicer->setChecked(true);
        dlg->reject();
        QTRY_VERIFY(dlg.isNull());
        dlg = open("print3d");
        QVERIFY(dlg);
        slicer = dlg->findChild<QCheckBox *>(QStringLiteral("exportOpenAfter"));
        QVERIFY(slicer && slicer->isChecked());
        // A scripted export (a preset path) is written but never handed to an app.
        QVERIFY(exportTo(dlg, path("scripted.stl")));
        QVERIFY(dlg->lastResult().ok);
        slicer->setChecked(false);
        QVERIFY(!QSettings().value(QStringLiteral("export/openInSlicer")).toBool());
    }

    void nothingToExport() {
        m_window->refresh();
        ExportDialog *dlg = open("print3d");
        QVERIFY(dlg);
        auto *button = dlg->findChild<QPushButton *>(QStringLiteral("exportButton"));
        QVERIFY(button);
        button->click();
        QVERIFY(dlg->report().contains(QStringLiteral("no bodies")));
        QVERIFY(!QFileInfo::exists(path("empty.stl")));
    }

    void theAppIconIsATileOnTransparency() {
        const QImage img = appIconImage(128);
        QCOMPARE(img.size(), QSize(128, 128));
        QCOMPARE(qAlpha(img.pixel(2, 2)), 0);        // outside the rounded tile
        QCOMPARE(qAlpha(img.pixel(64, 20)), 255);    // on the tile
        QVERIFY(qBlue(img.pixel(20, 64)) > qRed(img.pixel(20, 64))); // blue
        QVERIFY(!appIcon().isNull());
    }
};

CADJITSU_REGISTER_TEST(ExportTests)

#include "tst_export.moc"
