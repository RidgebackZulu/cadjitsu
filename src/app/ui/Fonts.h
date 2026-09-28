#pragma once

#include <QString>

namespace cadjitsu {

// Makes the fonts Cadjitsu ships with (for text) available: from the macOS
// bundle's Resources/fonts, a fonts folder next to the program, or the
// source tree in development builds. Returns the folder used (empty: none).
QString registerBundledFonts();

} // namespace cadjitsu
