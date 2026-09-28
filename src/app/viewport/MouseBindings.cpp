#include "viewport/MouseBindings.h"

#include <QCoreApplication>
#include <QSettings>

namespace cadjitsu {

MouseBindings MouseBindings::preset(Preset p) {
    MouseBindings b;
    switch(p) {
    case Preset::Cadjitsu:
    case Preset::Custom: break; // right drag orbits, middle drag pans (Shift + middle orbits too)
    case Preset::Fusion:
        b.orbit = Drag::ShiftMiddle;
        b.orbit2 = Drag::Off;
        b.pan = Drag::Middle;
        break;
    case Preset::SolidWorks:
        b.orbit = Drag::Middle;
        b.orbit2 = Drag::Off;
        b.pan = Drag::CtrlMiddle;
        b.zoom = Drag::ShiftMiddle;
        break;
    }
    return b;
}

MouseBindings::Preset MouseBindings::matchingPreset() const {
    for(Preset p : {Preset::Cadjitsu, Preset::Fusion, Preset::SolidWorks})
        if(*this == preset(p)) return p;
    return Preset::Custom;
}

QStringList MouseBindings::dragNames() {
#ifdef Q_OS_MACOS
    const QString ctrl = QStringLiteral("Cmd"), alt = QStringLiteral("Option");
#else
    const QString ctrl = QStringLiteral("Ctrl"), alt = QStringLiteral("Alt");
#endif
    return {QCoreApplication::translate("MouseBindings", "Off"),
            QCoreApplication::translate("MouseBindings", "Right drag"),
            QCoreApplication::translate("MouseBindings", "Shift + right drag"),
            QCoreApplication::translate("MouseBindings", "Middle drag"),
            QCoreApplication::translate("MouseBindings", "Shift + middle drag"),
            QCoreApplication::translate("MouseBindings", "%1 + middle drag").arg(ctrl),
            QCoreApplication::translate("MouseBindings", "%1 + left drag").arg(alt),
            QCoreApplication::translate("MouseBindings", "%1 + right drag").arg(alt)};
}

QStringList MouseBindings::presetNames() {
    return {QCoreApplication::translate("MouseBindings", "Cadjitsu (right drag orbits)"),
            QCoreApplication::translate("MouseBindings", "Fusion 360"),
            QCoreApplication::translate("MouseBindings", "SolidWorks"),
            QCoreApplication::translate("MouseBindings", "Custom")};
}

bool MouseBindings::matches(Drag d, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    mods &= Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    switch(d) {
    case Drag::Off: return false;
    case Drag::Right: return button == Qt::RightButton && mods == Qt::NoModifier;
    case Drag::ShiftRight: return button == Qt::RightButton && mods == Qt::ShiftModifier;
    case Drag::Middle: return button == Qt::MiddleButton && mods == Qt::NoModifier;
    case Drag::ShiftMiddle: return button == Qt::MiddleButton && mods == Qt::ShiftModifier;
    case Drag::CtrlMiddle: return button == Qt::MiddleButton && mods == Qt::ControlModifier;
    case Drag::AltLeft: return button == Qt::LeftButton && mods == Qt::AltModifier;
    case Drag::AltRight: return button == Qt::RightButton && mods == Qt::AltModifier;
    }
    return false;
}

MouseBindings MouseBindings::load() {
    QSettings s;
    MouseBindings b;
    auto drag = [&](const char *key, Drag def) {
        const int v = s.value(QStringLiteral("mouse/%1").arg(QLatin1String(key)), int(def)).toInt();
        return v >= 0 && v <= int(Drag::AltRight) ? Drag(v) : def;
    };
    b.orbit = drag("orbit", b.orbit);
    b.orbit2 = drag("orbit2", b.orbit2);
    b.pan = drag("pan", b.pan);
    b.pan2 = drag("pan2", b.pan2);
    b.zoom = drag("zoom", b.zoom);
    b.zoom2 = drag("zoom2", b.zoom2);
    b.invertWheel = s.value(QStringLiteral("mouse/invertWheel"), false).toBool();
    b.trackpadOrbits = s.value(QStringLiteral("mouse/trackpadOrbits"), false).toBool();
    return b;
}

void MouseBindings::save() const {
    QSettings s;
    s.setValue(QStringLiteral("mouse/orbit"), int(orbit));
    s.setValue(QStringLiteral("mouse/orbit2"), int(orbit2));
    s.setValue(QStringLiteral("mouse/pan"), int(pan));
    s.setValue(QStringLiteral("mouse/pan2"), int(pan2));
    s.setValue(QStringLiteral("mouse/zoom"), int(zoom));
    s.setValue(QStringLiteral("mouse/zoom2"), int(zoom2));
    s.setValue(QStringLiteral("mouse/invertWheel"), invertWheel);
    s.setValue(QStringLiteral("mouse/trackpadOrbits"), trackpadOrbits);
}

} // namespace cadjitsu
