#pragma once

#include "io/Exporter.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QThread;

namespace cad {
class Document;
}

namespace cadly {

class ModelView;

// File > Export and 3D Print: STL for printing (meshed, welded along the
// model's edges and checked to be watertight before it is written) or STEP
// (read back to check it). Exports run in the background.
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(ModelView &view, cad::Document &doc, QWidget *parent = nullptr);
    ~ExportDialog() override;

    void setFormat(ExportJob::Format f);
    ExportJob::Format format() const;
    // Exports to `path` without asking for a file name (tests, scripts). Such
    // exports are never handed to the slicer.
    void setOutputPath(const QString &path) { m_presetPath = path; }
    // Answer to "the mesh has problems, write it anyway?" when not asking.
    void setWriteInvalid(bool on) { m_writeInvalid = on; }
    void setAskBeforeWritingInvalid(bool on) { m_ask = on; }

    void startExport();
    bool busy() const { return m_busy; }
    const ExportResult &lastResult() const { return m_result; }
    QString report() const;

    QComboBox *refinementBox() const { return m_refinement; }
    QCheckBox *mergeBox() const { return m_merge; }

signals:
    void exportFinished(bool ok);

private:
    void updateRows();
    void run(const ExportJob &job, const QString &path, bool writeInvalid);
    void done(const ExportJob &job, const QString &path, const ExportResult &r);

    ModelView &m_view;
    cad::Document &m_doc;
    QComboBox *m_format, *m_bodies, *m_refinement, *m_schema;
    QDoubleSpinBox *m_chord, *m_angle;
    QCheckBox *m_binary, *m_merge, *m_openAfter;
    QGroupBox *m_stlBox, *m_stepBox;
    QLabel *m_report;
    QPushButton *m_export, *m_close;
    QString m_presetPath;
    bool m_writeInvalid = false;
    bool m_ask = true;
    bool m_busy = false;
    QThread *m_thread = nullptr;
    ExportResult m_result;
};

} // namespace cadly
