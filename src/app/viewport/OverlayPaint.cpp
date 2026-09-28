#include "viewport/OverlayPaint.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>

#include <cmath>

namespace cadjitsu {

void drawArrow(QPainter &p, QPointF tip, QPointF dir, const QColor &color) {
    const double l = std::hypot(dir.x(), dir.y());
    if(l < 1e-6) return;
    const QPointF u = dir / l, n(-u.y(), u.x());
    const QPointF base = tip - u * 8.0;
    QPolygonF tri;
    tri << tip << base + n * 2.8 << base - n * 2.8;
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(tri);
    p.restore();
}

QRectF drawLabelPlate(QPainter &p, QPointF centre, const QString &text, const QColor &border, const QColor &textColor,
                      int pixelSize) {
    p.save();
    QFont f = p.font();
    f.setPixelSize(pixelSize);
    p.setFont(f);
    const QFontMetricsF fm(f);
    QRectF r(0, 0, fm.horizontalAdvance(text) + 12, fm.height() + 4);
    r.moveCenter(centre);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(border, 1.0));
    p.setBrush(QColor(255, 255, 255, 240));
    p.drawRoundedRect(r, 4, 4);
    p.setPen(textColor);
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
    return r;
}

} // namespace cadjitsu
