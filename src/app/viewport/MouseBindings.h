#pragma once

#include <QString>
#include <QStringList>

#include <Qt>

namespace cadly {

// Which mouse drag orbits, pans and zooms the view. Each action can have two
// drags (for example right drag and Shift + middle drag both orbit). A plain
// right click (no drag) still opens the marking menu; left drags select.
struct MouseBindings {
    // The drags that can be bound (the order of the settings menus).
    enum class Drag { Off, Right, ShiftRight, Middle, ShiftMiddle, CtrlMiddle, AltLeft, AltRight };
    enum class Preset { Cadly, Fusion, SolidWorks, Custom };

    Drag orbit = Drag::Right, orbit2 = Drag::ShiftMiddle;
    Drag pan = Drag::Middle, pan2 = Drag::Off;
    Drag zoom = Drag::Off, zoom2 = Drag::Off;
    bool invertWheel = false;     // scroll up zooms out
    bool trackpadOrbits = false;  // two-finger drag orbits (Shift pans) instead of panning

    static MouseBindings preset(Preset p);
    // The preset these bindings are, or Custom.
    Preset matchingPreset() const;
    static QStringList dragNames();
    static QStringList presetNames();

    // Does a press of `button` with `mods` start this drag?
    static bool matches(Drag d, Qt::MouseButton button, Qt::KeyboardModifiers mods);

    static MouseBindings load();  // from the user's settings (the Cadly preset by default)
    void save() const;

    bool operator==(const MouseBindings &) const = default;
};

} // namespace cadly
