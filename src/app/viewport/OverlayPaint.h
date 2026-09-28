#pragma once

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

namespace cadjitsu {

// Small drawing helpers shared by canvas overlays (sketch dimensions, the
// measure tool): a filled arrow head and a value on a light rounded plate.
void drawArrow(QPainter &p, QPointF tip, QPointF dir, const QColor &color);
// Draws `text` centred at `centre`; returns the plate's rectangle.
QRectF drawLabelPlate(QPainter &p, QPointF centre, const QString &text, const QColor &border, const QColor &textColor,
                      int pixelSize = 12);

} // namespace cadjitsu
