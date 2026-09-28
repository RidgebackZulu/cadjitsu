#include "ui/ExportDialog.h"

#include "model/ModelView.h"

#include "doc/Document.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardItemModel>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace cadjitsu {

namespace {

QString number(double v, int decimals = 2) { return QLocale().toString(v, 'f', decimals); }

} // namespace

ExportDialog::ExportDialog(ModelView &view, cad::Document &doc, QWidget *parent)
    : QDialog(parent), m_view(view), m_doc(doc) {
    setWindowTitle(tr("Export"));
    setObjectName(QStringLiteral("exportDialog"));
    auto *v = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_format = new QComboBox(this);
    m_format->setObjectName(QStringLiteral("exportFormat"));
    m_format->addItems({tr("STL (3D printing)"), tr("STEP")});
    form->addRow(tr("Format"), m_format);
    m_bodies = new QComboBox(this);
    m_bodies->setObjectName(QStringLiteral("exportBodies"));
    const size_t visible = makeExportJob(view, doc, ExportJob::Format::Stl, false).solids.size();
    const size_t selected = makeExportJob(view, doc, ExportJob::Format::Stl, true).solids.size();
    m_bodies->addItem(tr("All visible bodies (%1)").arg(visible));
    m_bodies->addItem(tr("Selected bodies (%1)").arg(selected));
    if(selected > 0) m_bodies->setCurrentIndex(1);
    else if(auto *items = qobject_cast<QStandardItemModel *>(m_bodies->model())) items->item(1)->setEnabled(false);
    form->addRow(tr("Bodies"), m_bodies);
    v->addLayout(form);

    m_stlBox = new QGroupBox(tr("Mesh"), this);
    auto *stl = new QFormLayout(m_stlBox);
    m_refinement = new QComboBox(m_stlBox);
    m_refinement->setObjectName(QStringLiteral("exportRefinement"));
    m_refinement->addItems({tr("Coarse"), tr("Medium"), tr("Fine"), tr("Custom")});
    m_refinement->setCurrentIndex(1);
    stl->addRow(tr("Refinement"), m_refinement);
    m_chord = new QDoubleSpinBox(m_stlBox);
    m_chord->setDecimals(4);
    m_chord->setRange(0.0005, 1.0);
    m_chord->setSingleStep(0.005);
    m_chord->setValue(0.02);
    m_chord->setSuffix(QStringLiteral(" mm"));
    stl->addRow(tr("Chord tolerance"), m_chord);
    m_angle = new QDoubleSpinBox(m_stlBox);
    m_angle->setRange(1.0, 60.0);
    m_angle->setValue(15.0);
    m_angle->setSuffix(QStringLiteral(" deg"));
    stl->addRow(tr("Angle tolerance"), m_angle);
    m_binary = new QCheckBox(tr("Binary (smaller file)"), m_stlBox);
    m_binary->setChecked(true);
    stl->addRow(QString(), m_binary);
    m_merge = new QCheckBox(tr("Merge bodies into one solid"), m_stlBox);
    m_merge->setObjectName(QStringLiteral("exportMerge"));
    m_merge->setChecked(true);
    m_merge->setToolTip(tr("Bodies that touch or overlap are united, so the printer gets one watertight solid."));
    stl->addRow(QString(), m_merge);
    // Fusion's "Send to 3D print utility": hand the file to the slicer.
    m_openAfter = new QCheckBox(tr("Open in my slicer afterwards"), m_stlBox);
    m_openAfter->setObjectName(QStringLiteral("exportOpenAfter"));
    m_openAfter->setToolTip(tr("Opens the STL with the app your computer uses for .stl files "
                               "(PrusaSlicer, Bambu Studio, Cura…)."));
    m_openAfter->setChecked(QSettings().value(QStringLiteral("export/openInSlicer"), false).toBool());
    connect(m_openAfter, &QCheckBox::toggled, this,
            [](bool on) { QSettings().setValue(QStringLiteral("export/openInSlicer"), on); });
    stl->addRow(QString(), m_openAfter);
    v->addWidget(m_stlBox);

    m_stepBox = new QGroupBox(tr("STEP"), this);
    auto *step = new QFormLayout(m_stepBox);
    m_schema = new QComboBox(m_stepBox);
    m_schema->addItems({tr("AP242"), tr("AP214")});
    step->addRow(tr("Protocol"), m_schema);
    v->addWidget(m_stepBox);

    m_report = new QLabel(this);
    m_report->setObjectName(QStringLiteral("exportReport"));
    m_report->setWordWrap(true);
    m_report->setTextFormat(Qt::RichText);
    m_report->setMinimumWidth(380);
    v->addWidget(m_report);

    auto *buttons = new QDialogButtonBox(this);
    m_export = buttons->addButton(tr("Export…"), QDialogButtonBox::AcceptRole);
    m_export->setObjectName(QStringLiteral("exportButton"));
    m_export->setDefault(true);
    m_close = buttons->addButton(QDialogButtonBox::Close);
    v->addWidget(buttons);
    connect(m_export, &QPushButton::clicked, this, &ExportDialog::startExport);
    connect(m_close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_format, &QComboBox::currentIndexChanged, this, &ExportDialog::updateRows);
    connect(m_refinement, &QComboBox::currentIndexChanged, this, &ExportDialog::updateRows);
    updateRows();
}

ExportDialog::~ExportDialog() {
    // A running export finishes first (it only writes its file).
    if(m_thread) {
        m_thread->wait();
        delete m_thread;
    }
}

void ExportDialog::setFormat(ExportJob::Format f) { m_format->setCurrentIndex(f == ExportJob::Format::Stl ? 0 : 1); }

ExportJob::Format ExportDialog::format() const {
    return m_format->currentIndex() == 0 ? ExportJob::Format::Stl : ExportJob::Format::Step;
}

void ExportDialog::updateRows() {
    const bool stl = format() == ExportJob::Format::Stl;
    m_stlBox->setVisible(stl);
    m_stepBox->setVisible(!stl);
    // A preset shows its tolerances; Custom starts from the last one shown.
    const bool custom = m_refinement->currentIndex() == 3;
    if(!custom) {
        cad::StlOptions preset;
        const cad::StlResolution res[] = {cad::StlResolution::Coarse, cad::StlResolution::Medium, cad::StlResolution::Fine};
        preset.resolution = res[std::clamp(m_refinement->currentIndex(), 0, 2)];
        double chord = 0, angle = 0;
        cad::stlTolerances(preset, chord, angle);
        m_chord->setValue(chord);
        m_angle->setValue(angle);
    }
    m_chord->setEnabled(custom);
    m_angle->setEnabled(custom);
    adjustSize();
}

QString ExportDialog::report() const { return m_report->text(); }

void ExportDialog::startExport() {
    if(m_busy) return;
    ExportJob job = makeExportJob(m_view, m_doc, format(), m_bodies->currentIndex() == 1);
    if(job.solids.empty()) {
        m_report->setText(tr("There are no bodies to export."));
        return;
    }
    const cad::StlResolution res[] = {cad::StlResolution::Coarse, cad::StlResolution::Medium, cad::StlResolution::Fine,
                                      cad::StlResolution::Custom};
    job.stl.resolution = res[std::clamp(m_refinement->currentIndex(), 0, 3)];
    job.stl.deflection = m_chord->value();
    job.stl.angleDegrees = m_angle->value();
    job.stl.binary = m_binary->isChecked();
    job.stl.mergeBodies = m_merge->isChecked();
    job.schema = m_schema->currentIndex() == 0 ? cad::StepSchema::AP242 : cad::StepSchema::AP214;
    QString path = m_presetPath;
    if(path.isEmpty()) {
        const bool stl = job.format == ExportJob::Format::Stl;
        const QString name = QString::fromStdString(job.solids.size() == 1 ? job.solids.front().name : std::string("design"));
        QSettings settings;
        const QString dir = settings.value(QStringLiteral("export/lastDir")).toString();
        const QString file = name + (stl ? QStringLiteral(".stl") : QStringLiteral(".step"));
        path = QFileDialog::getSaveFileName(this, tr("Export"), dir.isEmpty() ? file : QDir(dir).filePath(file),
                                            stl ? tr("STL files (*.stl)") : tr("STEP files (*.step *.stp)"));
        if(path.isEmpty()) return;
        settings.setValue(QStringLiteral("export/lastDir"), QFileInfo(path).absolutePath());
    }
    run(job, path, m_writeInvalid);
}

// Meshing and writing happen on a worker thread; the result comes back here.
void ExportDialog::run(const ExportJob &job, const QString &path, bool writeInvalid) {
    m_busy = true;
    m_export->setEnabled(false);
    m_report->setText(job.format == ExportJob::Format::Stl ? tr("Meshing and checking…") : tr("Writing…"));
    auto result = std::make_shared<ExportResult>();
    m_thread = QThread::create([job, file = path.toStdString(), writeInvalid, result] {
        *result = runExport(job, file, writeInvalid);
    });
    connect(m_thread, &QThread::finished, this, [this, job, path, result] {
        m_thread->deleteLater();
        m_thread = nullptr;
        done(job, path, *result);
    });
    // OCCT's mesher and booleans recurse deeply; macOS gives secondary threads
    // only 512 KB of stack by default.
    m_thread->setStackSize(16u * 1024u * 1024u);
    m_thread->start();
}

void ExportDialog::done(const ExportJob &job, const QString &path, const ExportResult &r) {
    m_busy = false;
    m_export->setEnabled(true);
    m_result = r;
    const QString file = QFileInfo(path).fileName();
    QString text;
    if(r.meshChecked) {
        const cad::MeshReport &m = r.report;
        text += tr("%1 triangles, %2 shell(s)").arg(QLocale().toString(qulonglong(m.triangles))).arg(m.shells);
        text += m.ok ? tr(" · <b style='color:#1f7a3a'>watertight and printable</b>")
                     : tr(" · <b style='color:#b3261e'>not printable</b>");
        text += tr("<br>Mesh volume %1 mm³ (model %2 mm³)").arg(number(m.volume), number(r.solidVolume));
        if(!m.problems.empty()) {
            text += QStringLiteral("<ul>");
            for(const auto &p : m.problems) text += QStringLiteral("<li>%1</li>").arg(QString::fromStdString(p).toHtmlEscaped());
            text += QStringLiteral("</ul>");
        }
    }
    if(r.ok) {
        text = tr("<b>Wrote %1</b><br>").arg(file.toHtmlEscaped()) + text;
        if(r.reimported)
            text += tr("Read back: %1 mm³ (model %2 mm³)").arg(number(r.reimportedVolume), number(r.solidVolume));
    } else if(!r.meshChecked || r.report.ok) {
        text = tr("<b style='color:#b3261e'>Not exported:</b> %1<br>").arg(QString::fromStdString(r.error).toHtmlEscaped()) + text;
    }
    m_report->setText(text);
    adjustSize();
    // Only files the user chose go on to the slicer (scripted exports never open apps).
    if(r.ok && job.format == ExportJob::Format::Stl && m_openAfter->isChecked() && m_presetPath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    // A mesh that fails the check is written only if the user says so.
    if(!r.ok && r.meshChecked && !r.report.ok && m_ask) {
        const auto answer =
            QMessageBox::warning(this, tr("Export"), tr("The mesh has problems and may not print well:\n%1\n\nWrite it anyway?")
                                                         .arg(QString::fromStdString(r.report.summary())),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if(answer == QMessageBox::Yes) {
            run(job, path, true);
            return;
        }
    }
    emit exportFinished(r.ok);
}

} // namespace cadjitsu
