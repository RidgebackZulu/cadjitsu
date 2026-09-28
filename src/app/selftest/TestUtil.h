#pragma once

#include <QColor>

class QRhiWidget;

namespace cadjitsu {

// Spins the event loop until `widget` has submitted `frames` more frames, or
// the timeout expires. Returns false on timeout or render failure.
bool waitForFrames(QRhiWidget *widget, int frames = 1, int timeoutMs = 10000);

// Spins the event loop for roughly `ms` milliseconds.
void processEventsFor(int ms);

bool colorNear(const QColor &a, const QColor &b, int tolerance);

} // namespace cadjitsu
