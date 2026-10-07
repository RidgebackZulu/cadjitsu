#include "model/CanvasPicture.h"

#include "image/LensModel.h"

#include <QHash>
#include <QPainter>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstring>
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

QImage undistortPicture(const QImage &photo, const cad::LensDistortion &lens) {
    if(photo.isNull() || lens.none()) return photo;
    const QImage rgba = photo.convertToFormat(QImage::Format_RGBA8888);
    const cad::ImageView view{rgba.constBits(), rgba.width(), rgba.height(), int(rgba.bytesPerLine())};
    const std::vector<std::uint8_t> out = cad::undistortImage(view, lens);
    QImage img(rgba.width(), rgba.height(), QImage::Format_RGBA8888);
    for(int y = 0; y < img.height(); ++y)
        std::memcpy(img.scanLine(y), out.data() + size_t(y) * size_t(img.width()) * 4, size_t(img.width()) * 4);
    return img.convertToFormat(QImage::Format_ARGB32);
}

namespace {

QString lensKey(const cad::ReferenceImage &canvas) {
    QString key = QString::fromStdString(canvas.imageKey);
    if(canvas.lens && !canvas.lens->distortion.none())
        key += QStringLiteral("|lens %1 %2").arg(canvas.lens->distortion.k1, 0, 'g', 17).arg(canvas.lens->distortion.k2, 0, 'g', 17);
    return key;
}

} // namespace

QImage canvasSource(const cad::Document &doc, const cad::ReferenceImage &canvas) {
    const QImage photo = canvasPhoto(doc, canvas);
    if(photo.isNull() || !canvas.lens || canvas.lens->distortion.none()) return photo;
    return cached(lensKey(canvas), [&] { return undistortPicture(photo, canvas.lens->distortion); });
}

QImage canvasPicture(const cad::Document &doc, const cad::ReferenceImage &canvas) {
    const QImage photo = canvasSource(doc, canvas);
    if(photo.isNull() || !canvas.perspective) return photo;
    const auto &p = *canvas.perspective;
    QString key = lensKey(canvas);
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
    const QImage photo = canvasSource(doc, canvas);
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

TraceSource traceSource(const QImage &picture) {
    static qint64 key = 0;
    static TraceSource cachedSource;
    if(picture.isNull()) return {};
    if(key != picture.cacheKey() || cachedSource.small.isNull()) {
        const double scale = std::min(1.0, 1200.0 / std::max(picture.width(), picture.height()));
        cachedSource.scale = scale;
        cachedSource.small = (scale < 1.0 ? picture.scaled(std::max(1, int(picture.width() * scale)),
                                                           std::max(1, int(picture.height() * scale)),
                                                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                          : picture)
                                 .convertToFormat(QImage::Format_RGBA8888);
        key = picture.cacheKey();
    }
    return cachedSource;
}

std::vector<cad::FitLoop> traceRegion(const QImage &small, QPoint seed, double sensitivity, cad::Mask *maskOut) {
    std::vector<cad::FitLoop> loops;
    if(small.isNull() || !small.rect().contains(seed)) return loops;
    const QImage rgba = small.format() == QImage::Format_RGBA8888 ? small : small.convertToFormat(QImage::Format_RGBA8888);
    const cad::ImageView view{rgba.constBits(), rgba.width(), rgba.height(), int(rgba.bytesPerLine())};
    cad::Mask mask = cad::regionAt(view, seed.x(), seed.y(), std::clamp(sensitivity, 0.0, 100.0) / 100.0 * 0.6);
    // A region filling the whole picture is the background, not a part.
    if(mask.count() < size_t(rgba.width()) * size_t(rgba.height()) * 95 / 100) {
        const double minArea = std::max(12.0, 0.0002 * rgba.width() * rgba.height());
        const double tol = std::max(1.0, 0.0015 * std::max(rgba.width(), rgba.height()));
        for(const cad::TracedLoop &l : cad::traceLoops(mask, minArea)) loops.push_back(cad::fitContour(l.points, tol));
    }
    if(maskOut) *maskOut = std::move(mask);
    return loops;
}

cad::PixelBox partBox(const QImage &picture) {
    if(picture.isNull()) return {};
    // Found on a copy at most 800 pixels a side, then scaled back.
    const double s = std::min(1.0, 800.0 / std::max(picture.width(), picture.height()));
    const QImage small = (s < 1.0 ? picture.scaled(std::max(1, int(picture.width() * s)), std::max(1, int(picture.height() * s)),
                                                   Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                  : picture)
                             .convertToFormat(QImage::Format_RGBA8888);
    const cad::ImageView view{small.constBits(), small.width(), small.height(), int(small.bytesPerLine())};
    const cad::PixelBox b = cad::boundingBox(cad::foreground(view, 0.2));
    if(b.empty()) return b;
    const double kx = double(picture.width()) / small.width(), ky = double(picture.height()) / small.height();
    return {int(std::floor(b.x0 * kx)), int(std::floor(b.y0 * ky)),
            std::min(picture.width() - 1, int(std::ceil((b.x1 + 1) * kx)) - 1),
            std::min(picture.height() - 1, int(std::ceil((b.y1 + 1) * ky)) - 1)};
}

cad::ViewsResult viewCanvases(const cad::Document &doc, const std::vector<ViewPhoto> &views, int axis, double mm,
                              std::vector<cad::ReferenceImage> &out) {
    out.clear();
    std::vector<cad::ViewInput> in;
    for(const ViewPhoto &v : views) {
        cad::ReferenceImage probe;
        probe.imageKey = v.imageKey;
        const QImage photo = canvasPhoto(doc, probe);
        if(photo.isNull()) {
            cad::ViewsResult bad;
            bad.error = "a view's picture cannot be read";
            return bad;
        }
        static QHash<QString, cad::PixelBox> boxes; // finding the part takes a moment: once a picture
        const QString key = QString::fromStdString(v.imageKey);
        if(!boxes.contains(key)) boxes.insert(key, partBox(photo));
        in.push_back({v.side, photo.width(), photo.height(), boxes.value(key)});
    }
    cad::ViewsResult r = cad::alignViews(in, axis, mm);
    if(!r.ok) return r;
    for(size_t i = 0; i < views.size(); ++i) {
        cad::ReferenceImage c;
        const cad::ViewSide side = views[i].side;
        c.name = side == cad::ViewSide::Front ? "Front" : side == cad::ViewSide::Side ? "Side" : "Top";
        c.plane = cad::PlaneRef::origin(side == cad::ViewSide::Front  ? cad::PlaneRef::Kind::XZ
                                        : side == cad::ViewSide::Side ? cad::PlaneRef::Kind::YZ
                                                                      : cad::PlaneRef::Kind::XY);
        c.imageKey = views[i].imageKey;
        c.pixelWidth = in[i].width;
        c.pixelHeight = in[i].height;
        c.mmPerPixel = r.views[i].mmPerPixel;
        c.origin = r.views[i].origin;
        c.opacity = 0.6;
        out.push_back(c);
    }
    return r;
}

bool setCanvasLens(const cad::Document &doc, cad::ReferenceImage &canvas,
                   const std::optional<cad::ReferenceImage::Lens> &lens) {
    const QImage photo = canvasPhoto(doc, canvas);
    if(photo.isNull()) return false;
    const cad::LensDistortion before = canvas.lens ? canvas.lens->distortion : cad::LensDistortion{};
    const cad::LensDistortion after = lens ? lens->distortion : cad::LensDistortion{};
    canvas.lens = lens;
    if(!canvas.perspective) return true; // the same size: nothing else moves
    // The corners on the same spots of the photo, through the new lens.
    const cad::ReferenceImage::Perspective old = *canvas.perspective;
    std::array<cad::Vec2, 4> corners;
    for(size_t k = 0; k < 4; ++k)
        corners[k] = cad::undistortPixel(after, cad::distortPixel(before, old.corners[k], photo.width(), photo.height()),
                                         photo.width(), photo.height());
    // Where the sheet's middle is now: it stays there.
    double mm = 0;
    cad::Vec2 centre;
    const QImage oldSource = before.none() ? photo : undistortPicture(photo, before);
    if(correctPerspective(oldSource, old.corners, old.realWidth, old.realHeight, mm, &centre).isNull()) return false;
    canvas.origin = canvas.toPlane(centre);
    return applyPerspective(doc, canvas, corners, old.realWidth, old.realHeight);
}

} // namespace cadjitsu
