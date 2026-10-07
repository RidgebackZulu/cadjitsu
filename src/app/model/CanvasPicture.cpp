#include "model/CanvasPicture.h"

#include <QHash>
#include <QPainter>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <list>

namespace cadjitsu {

namespace {

constexpr int kMaxSide = 4096;

struct CacheEntry {
    QString key;
    QImage image;
};

// A few recent pictures (a design rarely has many canvases).
QImage cached(const QString &key, const std::function<QImage()> &make) {
    static std::list<CacheEntry> cache;
    for(auto it = cache.begin(); it != cache.end(); ++it) {
        if(it->key == key) {
            cache.splice(cache.begin(), cache, it);
            return cache.front().image;
        }
    }
    QImage img = make();
    cache.push_front({key, img});
    while(cache.size() > 8) cache.pop_back();
    return img;
}

} // namespace

QImage canvasPhoto(const cad::Document &doc, const cad::ReferenceImage &canvas) {
    const auto bytes = doc.image(canvas.imageKey);
    if(!bytes) return {};
    return cached(QString::fromStdString(canvas.imageKey), [&] {
        QImage img = QImage::fromData(reinterpret_cast<const uchar *>(bytes->data()), int(bytes->size()));
        return img.isNull() ? img : img.convertToFormat(QImage::Format_ARGB32);
    });
}

QImage canvasPicture(const cad::Document &doc, const cad::ReferenceImage &canvas) {
    const QImage photo = canvasPhoto(doc, canvas);
    if(photo.isNull() || !canvas.perspective) return photo;
    const auto &p = *canvas.perspective;
    QString key = QString::fromStdString(canvas.imageKey);
    for(const cad::Vec2 &c : p.corners) key += QStringLiteral("|%1,%2").arg(c.x).arg(c.y);
    key += QStringLiteral("|%1x%2").arg(p.realWidth).arg(p.realHeight);
    return cached(key, [&] {
        double mm = 0;
        const QImage out = correctPerspective(photo, p.corners, p.realWidth, p.realHeight, mm);
        return out.isNull() ? photo : out;
    });
}

std::array<cad::Vec2, 4> orderCorners(std::array<cad::Vec2, 4> c) {
    // Sort round the middle (angles in y-down pixels), then start at the one
    // nearest the top-left.
    cad::Vec2 mid;
    for(const auto &p : c) mid = mid + p * 0.25;
    std::sort(c.begin(), c.end(), [&](const cad::Vec2 &a, const cad::Vec2 &b) {
        return std::atan2(a.y - mid.y, a.x - mid.x) < std::atan2(b.y - mid.y, b.x - mid.x);
    });
    // Angles increase clockwise on screen (y down): from about -135 deg (top-left).
    int first = 0;
    double best = 1e300;
    for(int k = 0; k < 4; ++k) {
        const double s = c[k].x + c[k].y;
        if(s < best) {
            best = s;
            first = k;
        }
    }
    std::array<cad::Vec2, 4> out;
    for(int k = 0; k < 4; ++k) out[k] = c[(first + k) % 4];
    return out;
}

QImage correctPerspective(const QImage &photo, const std::array<cad::Vec2, 4> &corners, double realWidth,
                          double realHeight, double &mmPerPixel, cad::Vec2 *rectCentre) {
    if(photo.isNull() || !(realWidth > 0) || !(realHeight > 0)) return {};
    // Pixels per mm: about as sharp as the photo shows the rectangle.
    const double top = cad::distance(corners[0], corners[1]), bottom = cad::distance(corners[3], corners[2]);
    const double left = cad::distance(corners[0], corners[3]), right = cad::distance(corners[1], corners[2]);
    double k = std::max(std::max(top, bottom) / realWidth, std::max(left, right) / realHeight);
    if(!(k > 0)) return {};
    QPolygonF quad;
    for(const auto &c : corners) quad << QPointF(c.x, c.y);
    QTransform t;
    auto solve = [&](double scale) {
        QPolygonF rect;
        rect << QPointF(0, 0) << QPointF(realWidth * scale, 0) << QPointF(realWidth * scale, realHeight * scale)
             << QPointF(0, realHeight * scale);
        return QTransform::quadToQuad(quad, rect, t);
    };
    if(!solve(k)) return {};
    // The whole photo, warped (where it stays in front of the camera).
    QRectF box;
    bool any = false;
    const QPointF photoCorners[4] = {{0, 0}, {double(photo.width()), 0}, {double(photo.width()), double(photo.height())},
                                     {0, double(photo.height())}};
    for(const QPointF &pc : photoCorners) {
        const double w = t.m13() * pc.x() + t.m23() * pc.y() + t.m33();
        if(w <= 1e-9) continue;
        const QPointF q = t.map(pc);
        box = any ? box.united(QRectF(q, QSizeF(0, 0))) : QRectF(q, QSizeF(0, 0));
        any = true;
    }
    const QRectF rect(0, 0, realWidth * k, realHeight * k);
    box = any ? box.united(rect) : rect;
    // Keep it a sensible size: far parts of a steep photo stretch a lot.
    box = box.intersected(rect.adjusted(-rect.width() * 2, -rect.height() * 2, rect.width() * 2, rect.height() * 2));
    const double side = std::max(box.width(), box.height());
    if(side > kMaxSide) {
        const double s = kMaxSide / side;
        k *= s;
        if(!solve(k)) return {};
        box = QRectF(box.topLeft() * s, box.size() * s);
    }
    QImage out(std::max(1, int(std::ceil(box.width()))), std::max(1, int(std::ceil(box.height()))), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setTransform(t * QTransform::fromTranslate(-box.left(), -box.top()));
    painter.drawImage(QPointF(0, 0), photo);
    painter.end();
    mmPerPixel = 1.0 / k;
    if(rectCentre) *rectCentre = cad::Vec2(realWidth * k / 2 - box.left(), realHeight * k / 2 - box.top());
    return out;
}

bool applyPerspective(const cad::Document &doc, cad::ReferenceImage &canvas, const std::array<cad::Vec2, 4> &corners,
                      double realWidth, double realHeight) {
    const QImage photo = canvasPhoto(doc, canvas);
    if(photo.isNull()) return false;
    const std::array<cad::Vec2, 4> ordered = orderCorners(corners);
    double mm = 0.0;
    cad::Vec2 centre;
    const QImage out = correctPerspective(photo, ordered, realWidth, realHeight, mm, &centre);
    if(out.isNull()) return false;
    const cad::Vec2 middle = canvas.origin;
    canvas.perspective = cad::ReferenceImage::Perspective{ordered, realWidth, realHeight};
    canvas.mmPerPixel = mm;
    canvas.pixelWidth = out.width();
    canvas.pixelHeight = out.height();
    // The rectangle (the sheet the part lies on) centred where the picture was.
    canvas.origin = middle - (canvas.toPlane(centre) - canvas.origin);
    return true;
}

} // namespace cadjitsu
