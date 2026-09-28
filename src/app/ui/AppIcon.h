#pragma once

#include <QIcon>
#include <QImage>

namespace cadjitsu {

// The application icon, drawn in code: a bracket with holes (a typical printed
// part) on a blue tile, laid out like a macOS app icon (the tile inset in a
// transparent square). Needs only QtGui, so the macOS build can also run it to
// make Cadjitsu.icns.
QImage appIconImage(int size);
QIcon appIcon();

} // namespace cadjitsu
