#include "ui/AppIcon.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRadialGradient>

#include <cmath>

namespace cadly {

namespace {

// Isometric projection on the 1024 design grid: x to the right-front, y to the
// left-front, z up.
struct Iso {
    QPointF origin;
    double scale;
    QPointF operator()(double x, double y, double z) const {
        const double c = std::cos(M_PI / 6.0), s = 0.5;
        return origin + QPointF((x - y) * c * scale, ((x + y) * s - z) * scale);
    }
};

QPolygonF quad(const Iso &iso, const double (&pts)[4][3]) {
    QPolygonF q;
    for(const auto &p : pts) q << iso(p[0], p[1], p[2]);
    return q;
}

// A circle of radius r around `c`, in the plane spanned by u and v.
QPainterPath circle3d(const Iso &iso, const double (&c)[3], const double (&u)[3], const double (&v)[3], double r) {
    QPainterPath path;
    const int n = 96;
    for(int i = 0; i <= n; ++i) {
        const double a = 2.0 * M_PI * i / n, cu = r * std::cos(a), cv = r * std::sin(a);
        const QPointF p = iso(c[0] + cu * u[0] + cv * v[0], c[1] + cu * u[1] + cv * v[1], c[2] + cu * u[2] + cv * v[2]);
        if(i == 0) path.moveTo(p);
        else path.lineTo(p);
    }
    path.closeSubpath();
    return path;
}

void face(QPainter &p, const QPolygonF &poly, const QColor &top, const QColor &bottom) {
    const QRectF r = poly.boundingRect();
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, top);
    g.setColorAt(1, bottom);
    p.setBrush(g);
    p.drawPolygon(poly);
}

void hole(QPainter &p, const QPainterPath &rim, const QColor &inner) {
    const QRectF r = rim.boundingRect();
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, inner.darker(160));
    g.setColorAt(1, inner);
    p.setBrush(g);
    p.drawPath(rim);
}

void draw(QPainter &p) {
    // The tile: 824 of 1024, rounded, as macOS icons are laid out.
    const QRectF tile(100, 100, 824, 824);
    QPainterPath tilePath;
    tilePath.addRoundedRect(tile, 185, 185);
    // Soft drop shadow under the tile.
    for(int i = 0; i < 12; ++i) {
        QPainterPath sh;
        sh.addRoundedRect(tile.adjusted(-i, -i + 14, i, i + 14), 185 + i, 185 + i);
        p.fillPath(sh, QColor(0, 0, 0, 7));
    }
    QLinearGradient bg(tile.topLeft(), tile.bottomLeft());
    bg.setColorAt(0, QColor(58, 132, 230));
    bg.setColorAt(1, QColor(18, 58, 138));
    p.fillPath(tilePath, bg);
    p.save();
    p.setClipPath(tilePath);
    // A faint build-plate grid.
    p.setPen(QPen(QColor(255, 255, 255, 22), 3));
    const Iso grid{QPointF(512, 560), 300};
    for(int i = -4; i <= 4; ++i) {
        const double t = i * 0.35;
        p.drawLine(grid(t, -2.0, -0.02), grid(t, 2.0, -0.02));
        p.drawLine(grid(-2.0, t, -0.02), grid(2.0, t, -0.02));
    }
    // Contact shadow.
    QRadialGradient shadow(QPointF(512, 735), 330);
    shadow.setColorAt(0, QColor(4, 18, 48, 150));
    shadow.setColorAt(1, QColor(4, 18, 48, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(QPointF(512, 735), 330, 120);
    p.restore();

    // The part: an L bracket, base plate with a hole, upright with a hole.
    const Iso iso{QPointF(512, 520), 290};
    const QColor edge(255, 255, 255, 150);
    p.setPen(QPen(edge, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const double b = 0.30, t = 0.28, h = 1.0;
    // Base plate: top, right (+x) and left (+y) faces.
    const double baseTop[4][3] = {{0, 0, b}, {1, 0, b}, {1, 1, b}, {0, 1, b}};
    const double baseRight[4][3] = {{1, 0, b}, {1, 1, b}, {1, 1, 0}, {1, 0, 0}};
    const double baseLeft[4][3] = {{0, 1, b}, {1, 1, b}, {1, 1, 0}, {0, 1, 0}};
    face(p, quad(iso, baseTop), QColor(246, 249, 253), QColor(222, 231, 243));
    face(p, quad(iso, baseRight), QColor(168, 187, 212), QColor(140, 160, 190));
    face(p, quad(iso, baseLeft), QColor(203, 216, 234), QColor(178, 194, 218));
    const double c1[3] = {0.52, 0.66, b}, ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0}, ez[3] = {0, 0, 1};
    hole(p, circle3d(iso, c1, ex, ey, 0.17), QColor(62, 92, 140));
    // Upright at the back: front (+y), right (+x) and top faces.
    const double upFront[4][3] = {{0, t, b}, {1, t, b}, {1, t, h}, {0, t, h}};
    const double upRight[4][3] = {{1, 0, b}, {1, t, b}, {1, t, h}, {1, 0, h}};
    const double upTop[4][3] = {{0, 0, h}, {1, 0, h}, {1, t, h}, {0, t, h}};
    face(p, quad(iso, upFront), QColor(214, 226, 241), QColor(190, 205, 228));
    face(p, quad(iso, upRight), QColor(160, 180, 207), QColor(140, 160, 190));
    face(p, quad(iso, upTop), QColor(250, 252, 255), QColor(236, 242, 250));
    const double c2[3] = {0.5, t, 0.68};
    hole(p, circle3d(iso, c2, ex, ez, 0.15), QColor(70, 100, 150));
    // The selected edge, as Fusion highlights it.
    p.setPen(QPen(QColor(255, 170, 60), 9, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(iso(0, 1, b), iso(1, 1, b));
}

} // namespace

QImage appIconImage(int size) {
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 1024.0, size / 1024.0);
    draw(p);
    p.end();
    return img;
}

QIcon appIcon() {
    QIcon ic;
    for(int size : {16, 32, 48, 64, 128, 256, 512}) ic.addPixmap(QPixmap::fromImage(appIconImage(size)));
    return ic;
}

} // namespace cadly
