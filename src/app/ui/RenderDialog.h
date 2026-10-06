#pragma once

#include "doc/Document.h"

#include <QDialog>
#include <QPointer>

#include <map>
#include <vector>

class QAbstractButton;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QTimer;
class QToolButton;

namespace cadjitsu {

class ModelView;
class Viewport;

// RENDER (SOLID > INSPECT, View menu, a body's "Material..."): what each body
// is printed in and how the design is rendered, all applied at once to the
// canvas, which shows the Rendered style while the dialog is open.
//  - Bodies: which ones the material menus change (all by default).
//  - Material, Finish, Colour: PLA / PETG / TPU; matte / silk /
//    semitransparent; the finish's filament colours or any colour.
//  - Surface: layer height and layer lines.
//  - Build plate: textured or smooth PEI, or none; centred or as in a slicer.
//  - Scene: studio or daylight; live preview or ray traced; draft or final.
//  - Save Image: a path-traced picture of the view.
class RenderDialog : public QDialog {
    Q_OBJECT

public:
    RenderDialog(cad::Document &doc, ModelView *view, Viewport *viewport, QWidget *parent = nullptr);

    // Shows these bodies' material (and makes the menus change them).
    void selectBodies(const std::vector<cad::BodyId> &ids);

public slots:
    // Brings the controls up to date with the document.
    void sync();

public:
    // For tests.
    QListWidget *bodyList() const { return m_bodies; }
    QAbstractButton *materialButton(cad::PrintMaterial m) const;
    QAbstractButton *finishButton(cad::Finish f) const;
    QAbstractButton *plateButton(cad::BuildPlateKind k) const;
    QAbstractButton *lightingButton(cad::Lighting l) const;
    QAbstractButton *modeButton(bool rayTraced) const;
    std::vector<QToolButton *> colorButtons() const { return m_colorButtons; }
    QDoubleSpinBox *layerHeightBox() const { return m_layerHeight; }
    QCheckBox *layerLinesBox() const { return m_layerLines; }
    QComboBox *placementBox() const { return m_placement; }
    QLabel *statusLabel() const { return m_status; }
    // Renders and writes the picture (blocking; Save Image does it in the background).
    bool saveImage(const QString &path, const QSize &size, QString *error = nullptr);

protected:
    void showEvent(QShowEvent *e) override;

private:
    std::vector<cad::BodyId> targets() const;
    void applyMaterial(std::optional<cad::PrintMaterial> material, std::optional<cad::Finish> finish,
                       std::optional<std::pair<uint32_t, std::string>> color);
    void applySettings(const std::function<void(cad::RenderSettings &)> &change);
    void rebuildColors(cad::Finish finish, uint32_t rgb);
    void saveImageInteractive();
    void updateStatus();

    cad::Document &m_doc;
    QPointer<ModelView> m_view;
    QPointer<Viewport> m_viewport;
    bool m_syncing = false;

    QListWidget *m_bodies = nullptr;
    QButtonGroup *m_material = nullptr, *m_finish = nullptr, *m_plate = nullptr, *m_lighting = nullptr,
                 *m_mode = nullptr, *m_quality = nullptr;
    QGridLayout *m_colorGrid = nullptr;
    std::vector<QToolButton *> m_colorButtons;
    QToolButton *m_custom = nullptr;
    QDoubleSpinBox *m_layerHeight = nullptr;
    QCheckBox *m_layerLines = nullptr;
    QComboBox *m_placement = nullptr, *m_imageSize = nullptr;
    QPushButton *m_save = nullptr;
    QLabel *m_status = nullptr;
    QTimer *m_statusTimer = nullptr;
    bool m_saving = false;
};

} // namespace cadjitsu
