#include "ui/SettingsDialog.h"

#include "MainWindow.h"
#include "command/ExtrudeCommand.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>

#include "ui/Units.h"
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace cadjitsu {

SettingsDialog::SettingsDialog(const MouseBindings &current, QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setObjectName(QStringLiteral("settingsDialog"));
    auto *v = new QVBoxLayout(this);
    auto *mouse = new QGroupBox(tr("Mouse"), this);
    auto *form = new QFormLayout(mouse);
    m_preset = new QComboBox(mouse);
    m_preset->setObjectName(QStringLiteral("mousePreset"));
    m_preset->addItems(MouseBindings::presetNames());
    form->addRow(tr("Preset"), m_preset);
    auto pair = [&](const QString &label, QComboBox **boxes, const char *name) {
        auto *row = new QHBoxLayout;
        for(int i = 0; i < 2; ++i) {
            boxes[i] = new QComboBox(mouse);
            boxes[i]->setObjectName(QStringLiteral("%1%2").arg(QLatin1String(name)).arg(i + 1));
            boxes[i]->addItems(MouseBindings::dragNames());
            row->addWidget(boxes[i]);
            if(i == 0) row->addWidget(new QLabel(tr("or"), mouse));
            connect(boxes[i], &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePreset);
        }
        form->addRow(label, row);
    };
    pair(tr("Orbit"), m_orbit, "orbit");
    pair(tr("Pan"), m_pan, "pan");
    pair(tr("Zoom"), m_zoom, "zoom");
    m_invert = new QCheckBox(tr("Scroll up zooms out"), mouse);
    m_trackpad = new QCheckBox(tr("Trackpad: two-finger drag orbits (Shift pans)"), mouse);
    form->addRow(QString(), m_invert);
    form->addRow(QString(), m_trackpad);
    connect(m_invert, &QCheckBox::toggled, this, &SettingsDialog::updatePreset);
    connect(m_trackpad, &QCheckBox::toggled, this, &SettingsDialog::updatePreset);
    auto *note = new QLabel(tr("Left drag always selects. A right click that does not move opens the marking menu; "
                               "the scroll wheel and pinch zoom towards the cursor."),
                            mouse);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #5a6270;"));
    form->addRow(note);
    v->addWidget(mouse);

    auto *unitsBox = new QGroupBox(tr("Units"), this);
    auto *uf = new QFormLayout(unitsBox);
    m_lengthUnit = new QComboBox(unitsBox);
    m_lengthUnit->setObjectName(QStringLiteral("lengthUnit"));
    const QStringList lengthNames = {tr("Millimetres (mm)"), tr("Centimetres (cm)"), tr("Metres (m)"),
                                     tr("Inches (in)"), tr("Feet (ft)")};
    const QStringList lengthUnits = units::choices(cad::ValueKind::Length);
    for(int i = 0; i < lengthUnits.size(); ++i) m_lengthUnit->addItem(lengthNames.value(i, lengthUnits[i]), lengthUnits[i]);
    m_lengthUnit->setCurrentIndex(std::max(0, int(lengthUnits.indexOf(units::defaultUnit(cad::ValueKind::Length)))));
    uf->addRow(tr("Default length unit"), m_lengthUnit);
    auto *unitNote = new QLabel(tr("Value boxes show numbers in this unit. Pick another unit from a box's drop-down to "
                                   "type in it; the value is converted."),
                                unitsBox);
    unitNote->setWordWrap(true);
    unitNote->setStyleSheet(QStringLiteral("color: #5a6270;"));
    uf->addRow(unitNote);
    v->addWidget(unitsBox);

    auto *modeling = new QGroupBox(tr("Extrude"), this);
    auto *mv = new QVBoxLayout(modeling);
    m_autoOperation = new QCheckBox(tr("Choose Join / Cut automatically (as Fusion 360 does)"), modeling);
    m_autoOperation->setObjectName(QStringLiteral("autoOperation"));
    m_autoOperation->setChecked(ExtrudeCommand::autoOperation());
    mv->addWidget(m_autoOperation);
    auto *opNote = new QLabel(tr("Off: every extrude makes a new body; join or subtract bodies afterwards with Combine."),
                              modeling);
    opNote->setWordWrap(true);
    opNote->setStyleSheet(QStringLiteral("color: #5a6270;"));
    mv->addWidget(opNote);
    v->addWidget(modeling);

    auto *sketching = new QGroupBox(tr("Sketching"), this);
    auto *sv = new QVBoxLayout(sketching);
    m_liveBodies = new QCheckBox(tr("Update bodies while sketching"), sketching);
    m_liveBodies->setObjectName(QStringLiteral("liveSketchBodies"));
    m_liveBodies->setChecked(MainWindow::liveSketchBodies());
    sv->addWidget(m_liveBodies);
    auto *liveNote = new QLabel(tr("Bodies made from the open sketch reshape as you drag or move its geometry. On "
                                   "heavy models they wait until you let go of the mouse."),
                                sketching);
    liveNote->setWordWrap(true);
    liveNote->setStyleSheet(QStringLiteral("color: #5a6270;"));
    sv->addWidget(liveNote);
    v->addWidget(sketching);
    connect(m_preset, &QComboBox::activated, this, [this](int i) {
        if(i < int(MouseBindings::Preset::Custom)) showBindings(MouseBindings::preset(MouseBindings::Preset(i)));
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);
    showBindings(current);
}

void SettingsDialog::showBindings(const MouseBindings &b) {
    m_updating = true;
    m_orbit[0]->setCurrentIndex(int(b.orbit));
    m_orbit[1]->setCurrentIndex(int(b.orbit2));
    m_pan[0]->setCurrentIndex(int(b.pan));
    m_pan[1]->setCurrentIndex(int(b.pan2));
    m_zoom[0]->setCurrentIndex(int(b.zoom));
    m_zoom[1]->setCurrentIndex(int(b.zoom2));
    m_invert->setChecked(b.invertWheel);
    m_trackpad->setChecked(b.trackpadOrbits);
    m_updating = false;
    updatePreset();
}

void SettingsDialog::updatePreset() {
    if(m_updating) return;
    m_preset->setCurrentIndex(int(bindings().matchingPreset()));
}

bool SettingsDialog::autoOperation() const { return m_autoOperation->isChecked(); }
bool SettingsDialog::liveSketchBodies() const { return m_liveBodies->isChecked(); }
QString SettingsDialog::lengthUnit() const { return m_lengthUnit->currentData().toString(); }

MouseBindings SettingsDialog::bindings() const {
    MouseBindings b;
    using D = MouseBindings::Drag;
    b.orbit = D(m_orbit[0]->currentIndex());
    b.orbit2 = D(m_orbit[1]->currentIndex());
    b.pan = D(m_pan[0]->currentIndex());
    b.pan2 = D(m_pan[1]->currentIndex());
    b.zoom = D(m_zoom[0]->currentIndex());
    b.zoom2 = D(m_zoom[1]->currentIndex());
    b.invertWheel = m_invert->isChecked();
    b.trackpadOrbits = m_trackpad->isChecked();
    return b;
}

} // namespace cadjitsu
