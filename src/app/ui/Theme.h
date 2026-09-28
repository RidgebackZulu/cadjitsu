#pragma once

class QApplication;

namespace cadjitsu {

// Cadjitsu's panels are drawn light (as in Fusion 360), so the app always uses a
// light palette: with macOS in dark mode the system would otherwise hand the
// widgets white text on our light backgrounds.
void applyLightTheme(QApplication &app);

} // namespace cadjitsu
