#pragma once

#include "ui/Theme.h"
#include "viewport/MouseBindings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;

namespace cadjitsu {

// Cadjitsu > Settings: which mouse drags orbit, pan and zoom the view.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const MouseBindings &current, QWidget *parent = nullptr);
    MouseBindings bindings() const;
    bool autoOperation() const;
    bool liveSketchBodies() const;
    QString lengthUnit() const;
    ThemeMode theme() const;
    QComboBox *themeBox() const { return m_theme; }
    QComboBox *lengthUnitBox() const { return m_lengthUnit; }

    QComboBox *presetBox() const { return m_preset; }
    QComboBox *orbitBox() const { return m_orbit[0]; }

private:
    void showBindings(const MouseBindings &b);
    void updatePreset();

    QComboBox *m_preset, *m_lengthUnit, *m_theme;
    QComboBox *m_orbit[2], *m_pan[2], *m_zoom[2];
    QCheckBox *m_invert, *m_trackpad, *m_autoOperation, *m_liveBodies;
    bool m_updating = false;
};

} // namespace cadjitsu
