#pragma once

#include "viewport/MouseBindings.h"

#include <QDialog>

class QCheckBox;
class QComboBox;

namespace cadly {

// Cadly > Settings: which mouse drags orbit, pan and zoom the view.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const MouseBindings &current, QWidget *parent = nullptr);
    MouseBindings bindings() const;

    QComboBox *presetBox() const { return m_preset; }
    QComboBox *orbitBox() const { return m_orbit[0]; }

private:
    void showBindings(const MouseBindings &b);
    void updatePreset();

    QComboBox *m_preset;
    QComboBox *m_orbit[2], *m_pan[2], *m_zoom[2];
    QCheckBox *m_invert, *m_trackpad;
    bool m_updating = false;
};

} // namespace cadly
