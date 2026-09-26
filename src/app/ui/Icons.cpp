#include "ui/Icons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

#include <cmath>

namespace cadly {

namespace {

const QColor kInk(70, 78, 90);
const QColor kFill(214, 226, 240);

// All icons are drawn on a 32 x 32 design grid.
void drawIcon(QPainter &p, IconId id, const QColor &accent) {
    QPen ink(kInk, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen acc(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(ink);
    p.setBrush(Qt::NoBrush);

    auto arrowHead = [&](QPointF tip, double angle, double size = 5.0) {
        QPolygonF tri;
        tri << tip << tip + QPointF(-size * std::cos(angle - 0.45), -size * std::sin(angle - 0.45))
            << tip + QPointF(-size * std::cos(angle + 0.45), -size * std::sin(angle + 0.45));
        p.save();
        p.setBrush(p.pen().color());
        p.drawPolygon(tri);
        p.restore();
    };
    auto box3d = [&](QRectF front, double depth, bool fill) {
        const QPointF d(depth, -depth);
        QPolygonF top, side;
        top << front.topLeft() << front.topLeft() + d << front.topRight() + d << front.topRight();
        side << front.topRight() << front.topRight() + d << front.bottomRight() + d << front.bottomRight();
        p.save();
        if(fill) p.setBrush(kFill);
        p.drawPolygon(top);
        p.drawPolygon(side);
        p.drawRect(front);
        p.restore();
    };

    switch(id) {
    case IconId::Home: {
        QPolygonF house;
        house << QPointF(6, 15) << QPointF(16, 6) << QPointF(26, 15) << QPointF(23, 15) << QPointF(23, 26)
              << QPointF(9, 26) << QPointF(9, 15);
        p.setBrush(kFill);
        p.drawPolygon(house);
        p.drawRect(QRectF(14, 19, 4, 7));
        break;
    }
    case IconId::Orbit: {
        p.setPen(acc);
        p.drawArc(QRectF(5, 9, 22, 14), 30 * 16, 300 * 16);
        arrowHead(QPointF(25.5, 12.5), -0.9);
        p.setPen(ink);
        p.setBrush(kFill);
        p.drawEllipse(QPointF(16, 16), 4, 4);
        break;
    }
    case IconId::Pan: {
        p.setPen(acc);
        p.drawLine(QPointF(16, 5), QPointF(16, 27));
        p.drawLine(QPointF(5, 16), QPointF(27, 16));
        arrowHead(QPointF(16, 4), -M_PI / 2);
        arrowHead(QPointF(16, 28), M_PI / 2);
        arrowHead(QPointF(4, 16), M_PI);
        arrowHead(QPointF(28, 16), 0);
        break;
    }
    case IconId::Zoom: {
        p.setBrush(kFill);
        p.drawEllipse(QPointF(13, 13), 8, 8);
        p.setPen(QPen(kInk, 3.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(19, 19), QPointF(27, 27));
        p.setPen(acc);
        p.drawLine(QPointF(9.5, 13), QPointF(16.5, 13));
        p.drawLine(QPointF(13, 9.5), QPointF(13, 16.5));
        break;
    }
    case IconId::Fit: {
        p.setPen(acc);
        for(auto c : {QPointF(5, 5), QPointF(27, 5), QPointF(27, 27), QPointF(5, 27)}) {
            const double sx = c.x() < 16 ? 1 : -1, sy = c.y() < 16 ? 1 : -1;
            p.drawLine(c, c + QPointF(6 * sx, 0));
            p.drawLine(c, c + QPointF(0, 6 * sy));
        }
        p.setPen(ink);
        box3d(QRectF(10, 13, 10, 9), 3, true);
        break;
    }
    case IconId::Display: {
        box3d(QRectF(6, 12, 14, 14), 6, true);
        break;
    }
    case IconId::Grid: {
        p.setPen(QPen(kInk, 1.4));
        for(int i = 0; i <= 4; ++i) {
            p.drawLine(QPointF(5 + i * 5.5, 5), QPointF(5 + i * 5.5, 27));
            p.drawLine(QPointF(5, 5 + i * 5.5), QPointF(27, 5 + i * 5.5));
        }
        break;
    }
    case IconId::Camera: {
        p.setBrush(kFill);
        p.drawRoundedRect(QRectF(4, 10, 20, 14), 2, 2);
        QPolygonF lens;
        lens << QPointF(24, 14) << QPointF(29, 11) << QPointF(29, 23) << QPointF(24, 20);
        p.drawPolygon(lens);
        break;
    }
    case IconId::Sketch: {
        p.setBrush(kFill);
        p.drawRect(QRectF(5, 9, 18, 16));
        p.setPen(acc);
        p.drawLine(QPointF(12, 24), QPointF(26, 8));
        p.setPen(QPen(accent.darker(130), 2.0));
        p.drawLine(QPointF(24, 6), QPointF(28, 10));
        break;
    }
    case IconId::FinishSketch: {
        p.setBrush(QColor(90, 170, 90));
        p.setPen(QPen(QColor(50, 120, 50), 1.6));
        p.drawRoundedRect(QRectF(4, 4, 24, 24), 4, 4);
        p.setPen(QPen(Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPolygonF check;
        check << QPointF(9, 16.5) << QPointF(14, 21.5) << QPointF(23, 10.5);
        p.drawPolyline(check);
        break;
    }
    case IconId::Line: {
        p.setPen(acc);
        p.drawLine(QPointF(6, 26), QPointF(26, 6));
        p.setPen(ink);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(3.5, 23.5, 5, 5));
        p.drawRect(QRectF(23.5, 3.5, 5, 5));
        break;
    }
    case IconId::Rectangle: {
        p.setPen(acc);
        p.drawRect(QRectF(5, 8, 22, 16));
        p.setPen(ink);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(2.5, 5.5, 5, 5));
        p.drawRect(QRectF(24.5, 21.5, 5, 5));
        break;
    }
    case IconId::Circle: {
        p.setPen(acc);
        p.drawEllipse(QPointF(16, 16), 11, 11);
        p.setPen(ink);
        p.drawLine(QPointF(16, 16), QPointF(24, 8));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(16, 16), 2.2, 2.2);
        break;
    }
    case IconId::Arc: {
        p.setPen(acc);
        p.drawArc(QRectF(5, 7, 22, 22), 20 * 16, 140 * 16);
        p.setPen(ink);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(24, 11, 5, 5));
        p.drawRect(QRectF(3, 11, 5, 5));
        p.drawRect(QRectF(13.5, 4.5, 5, 5));
        break;
    }
    case IconId::Point: {
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(16, 16), 4.5, 4.5);
        break;
    }
    case IconId::Dimension: {
        p.drawLine(QPointF(5, 8), QPointF(5, 24));
        p.drawLine(QPointF(27, 8), QPointF(27, 24));
        p.setPen(acc);
        p.drawLine(QPointF(7, 16), QPointF(25, 16));
        arrowHead(QPointF(6, 16), M_PI);
        arrowHead(QPointF(26, 16), 0);
        break;
    }
    case IconId::Construction: {
        p.setPen(QPen(QColor(220, 130, 40), 2.2, Qt::DashLine, Qt::RoundCap));
        p.drawLine(QPointF(6, 26), QPointF(26, 6));
        break;
    }
    case IconId::Coincident: {
        p.drawLine(QPointF(5, 26), QPointF(16, 16));
        p.drawLine(QPointF(16, 16), QPointF(27, 22));
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(16, 16), 3.5, 3.5);
        break;
    }
    case IconId::Horizontal: {
        p.setPen(acc);
        p.drawLine(QPointF(5, 16), QPointF(27, 16));
        p.setPen(ink);
        p.drawText(QRectF(0, 0, 32, 14), Qt::AlignCenter, QStringLiteral("H"));
        break;
    }
    case IconId::Vertical: {
        p.setPen(acc);
        p.drawLine(QPointF(16, 5), QPointF(16, 27));
        p.setPen(ink);
        p.drawText(QRectF(18, 0, 14, 32), Qt::AlignCenter, QStringLiteral("V"));
        break;
    }
    case IconId::Parallel: {
        p.setPen(acc);
        p.drawLine(QPointF(6, 22), QPointF(20, 6));
        p.drawLine(QPointF(12, 26), QPointF(26, 10));
        break;
    }
    case IconId::Perpendicular: {
        p.setPen(acc);
        p.drawLine(QPointF(6, 26), QPointF(26, 26));
        p.drawLine(QPointF(16, 26), QPointF(16, 6));
        p.setPen(ink);
        p.drawRect(QRectF(16, 20, 6, 6));
        break;
    }
    case IconId::Tangent: {
        p.setPen(acc);
        p.drawEllipse(QPointF(16, 18), 8, 8);
        p.setPen(ink);
        p.drawLine(QPointF(4, 10), QPointF(28, 10));
        break;
    }
    case IconId::Equal: {
        p.setPen(acc);
        p.drawLine(QPointF(7, 12), QPointF(25, 12));
        p.drawLine(QPointF(7, 20), QPointF(25, 20));
        break;
    }
    case IconId::Midpoint: {
        p.drawLine(QPointF(4, 22), QPointF(28, 10));
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        QPolygonF tri;
        tri << QPointF(16, 11) << QPointF(21, 19) << QPointF(11, 19);
        p.drawPolygon(tri);
        break;
    }
    case IconId::Concentric: {
        p.setPen(acc);
        p.drawEllipse(QPointF(16, 16), 11, 11);
        p.drawEllipse(QPointF(16, 16), 5.5, 5.5);
        break;
    }
    case IconId::Fix: {
        p.setBrush(kFill);
        p.drawRoundedRect(QRectF(8, 14, 16, 13), 2, 2);
        p.drawArc(QRectF(10.5, 6, 11, 14), 0, 180 * 16);
        break;
    }
    case IconId::Extrude: {
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 60));
        p.setPen(acc);
        p.drawRect(QRectF(5, 18, 16, 9));
        p.setPen(ink);
        box3d(QRectF(5, 11, 16, 16), 6, false);
        p.setPen(acc);
        p.drawLine(QPointF(13, 17), QPointF(13, 5));
        arrowHead(QPointF(13, 3.5), -M_PI / 2);
        break;
    }
    case IconId::Fillet: {
        QPainterPath path;
        path.moveTo(5, 27);
        path.lineTo(5, 5);
        path.lineTo(15, 5);
        path.quadTo(27, 5, 27, 17);
        path.lineTo(27, 27);
        path.closeSubpath();
        p.setBrush(kFill);
        p.drawPath(path);
        p.setPen(acc);
        QPainterPath arc;
        arc.moveTo(15, 5);
        arc.quadTo(27, 5, 27, 17);
        p.setBrush(Qt::NoBrush);
        p.drawPath(arc);
        break;
    }
    case IconId::Chamfer: {
        QPolygonF poly;
        poly << QPointF(5, 27) << QPointF(5, 5) << QPointF(17, 5) << QPointF(27, 15) << QPointF(27, 27);
        p.setBrush(kFill);
        p.drawPolygon(poly);
        p.setPen(acc);
        p.drawLine(QPointF(17, 5), QPointF(27, 15));
        break;
    }
    case IconId::Hole: {
        box3d(QRectF(4, 12, 18, 14), 6, true);
        p.setPen(acc);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(16, 11.5), 5, 2.2);
        p.drawLine(QPointF(11, 11.5), QPointF(11, 20));
        p.drawLine(QPointF(21, 11.5), QPointF(21, 20));
        break;
    }
    case IconId::Combine: {
        p.setBrush(kFill);
        p.drawRect(QRectF(4, 10, 14, 14));
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 90));
        p.setPen(acc);
        p.drawEllipse(QPointF(20, 16), 8, 8);
        break;
    }
    case IconId::Plane: {
        QPolygonF pl;
        pl << QPointF(4, 22) << QPointF(12, 10) << QPointF(28, 10) << QPointF(20, 22);
        p.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 70));
        p.setPen(acc);
        p.drawPolygon(pl);
        p.setPen(ink);
        p.drawLine(QPointF(16, 16), QPointF(16, 4));
        arrowHead(QPointF(16, 3), -M_PI / 2, 4.0);
        break;
    }
    case IconId::Section: {
        box3d(QRectF(5, 11, 16, 16), 6, true);
        p.setPen(QPen(QColor(210, 60, 60), 2.0));
        p.setBrush(QColor(210, 60, 60, 60));
        QPolygonF cut;
        cut << QPointF(13, 11) << QPointF(19, 5) << QPointF(19, 21) << QPointF(13, 27);
        p.drawPolygon(cut);
        break;
    }
    case IconId::Measure: {
        p.setBrush(kFill);
        p.drawRect(QRectF(4, 12, 24, 9));
        for(int i = 0; i < 6; ++i) p.drawLine(QPointF(7 + i * 4, 12), QPointF(7 + i * 4, i % 2 ? 15 : 17));
        break;
    }
    case IconId::Undo:
    case IconId::Redo: {
        const bool undo = id == IconId::Undo;
        p.save();
        if(!undo) {
            p.translate(32, 0);
            p.scale(-1, 1);
        }
        p.setPen(acc);
        QPainterPath path;
        path.moveTo(8, 13);
        path.lineTo(20, 13);
        path.quadTo(27, 13, 27, 19.5);
        path.quadTo(27, 26, 20, 26);
        path.lineTo(13, 26);
        p.drawPath(path);
        arrowHead(QPointF(6, 13), M_PI, 6);
        p.restore();
        break;
    }
    case IconId::Save: {
        p.setBrush(kFill);
        p.drawRoundedRect(QRectF(5, 5, 22, 22), 2, 2);
        p.setBrush(Qt::white);
        p.drawRect(QRectF(10, 5, 12, 8));
        p.setPen(acc);
        p.drawRect(QRectF(9, 18, 14, 9));
        break;
    }
    case IconId::Open: {
        QPolygonF folder;
        folder << QPointF(4, 9) << QPointF(12, 9) << QPointF(14, 11) << QPointF(27, 11) << QPointF(27, 26)
               << QPointF(4, 26);
        p.setBrush(QColor(240, 214, 140));
        p.setPen(QPen(QColor(150, 120, 50), 1.8));
        p.drawPolygon(folder);
        break;
    }
    case IconId::New: {
        QPolygonF page;
        page << QPointF(7, 4) << QPointF(19, 4) << QPointF(25, 10) << QPointF(25, 28) << QPointF(7, 28);
        p.setBrush(Qt::white);
        p.drawPolygon(page);
        p.drawPolyline(QPolygonF() << QPointF(19, 4) << QPointF(19, 10) << QPointF(25, 10));
        break;
    }
    case IconId::ExportStl:
    case IconId::ExportStep: {
        box3d(QRectF(4, 12, 13, 13), 5, true);
        p.setPen(acc);
        p.drawLine(QPointF(18, 22), QPointF(28, 12));
        arrowHead(QPointF(29, 11), -M_PI / 4, 6);
        p.setPen(ink);
        QFont f = p.font();
        f.setPixelSize(8);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, 24, 32, 8), Qt::AlignCenter, id == IconId::ExportStl ? QStringLiteral("STL") : QStringLiteral("STEP"));
        break;
    }
    case IconId::Eye:
    case IconId::EyeOff: {
        QPainterPath eye;
        eye.moveTo(4, 16);
        eye.quadTo(16, 5, 28, 16);
        eye.quadTo(16, 27, 4, 16);
        p.setBrush(id == IconId::Eye ? QColor(255, 255, 255) : QColor(230, 230, 230));
        p.drawPath(eye);
        if(id == IconId::Eye) {
            p.setBrush(kInk);
            p.drawEllipse(QPointF(16, 16), 4, 4);
        } else {
            p.setPen(QPen(kInk, 2.2));
            p.drawLine(QPointF(6, 26), QPointF(26, 6));
        }
        break;
    }
    case IconId::Body: {
        box3d(QRectF(5, 12, 15, 15), 6, true);
        break;
    }
    case IconId::SketchNode: {
        p.setBrush(kFill);
        p.drawRect(QRectF(5, 7, 22, 18));
        p.setPen(acc);
        p.drawEllipse(QPointF(16, 16), 5, 5);
        break;
    }
    case IconId::PlaneNode: {
        QPolygonF pl;
        pl << QPointF(3, 23) << QPointF(11, 9) << QPointF(29, 9) << QPointF(21, 23);
        p.setBrush(QColor(240, 200, 120, 160));
        p.setPen(QPen(QColor(180, 130, 40), 1.8));
        p.drawPolygon(pl);
        break;
    }
    case IconId::Folder: {
        QPolygonF folder;
        folder << QPointF(4, 9) << QPointF(12, 9) << QPointF(14, 11) << QPointF(27, 11) << QPointF(27, 25)
               << QPointF(4, 25);
        p.setBrush(QColor(236, 206, 128));
        p.setPen(QPen(QColor(160, 128, 56), 1.6));
        p.drawPolygon(folder);
        break;
    }
    case IconId::Warning: {
        QPolygonF tri;
        tri << QPointF(16, 4) << QPointF(29, 27) << QPointF(3, 27);
        p.setBrush(QColor(245, 190, 40));
        p.setPen(QPen(QColor(150, 110, 10), 1.6));
        p.drawPolygon(tri);
        p.setPen(QPen(QColor(60, 40, 0), 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(16, 12), QPointF(16, 19));
        p.drawPoint(QPointF(16, 23.5));
        break;
    }
    case IconId::Error: {
        p.setBrush(QColor(215, 60, 55));
        p.setPen(QPen(QColor(140, 30, 30), 1.6));
        p.drawEllipse(QPointF(16, 16), 12, 12);
        p.setPen(QPen(Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(11.5, 11.5), QPointF(20.5, 20.5));
        p.drawLine(QPointF(20.5, 11.5), QPointF(11.5, 20.5));
        break;
    }
    }
}

} // namespace

QIcon icon(IconId id, const QColor &accent) {
    static QHash<QString, QIcon> cache;
    const QString key = QString::number(int(id)) + accent.name();
    auto it = cache.constFind(key);
    if(it != cache.constEnd()) return *it;
    QIcon ic;
    for(int size : {32, 64, 96}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(size / 32.0, size / 32.0);
        drawIcon(p, id, accent);
        p.end();
        ic.addPixmap(pm);
    }
    cache.insert(key, ic);
    return ic;
}

} // namespace cadly
