#include "ui/Icons.h"

#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QSvgRenderer>

#include <array>
#include <memory>

namespace cadly {

namespace {

struct Entry {
    IconId id;
    const char *name;
};

constexpr std::array kIcons = {
    Entry{IconId::Home, "home"},
    Entry{IconId::Orbit, "orbit"},
    Entry{IconId::Pan, "pan"},
    Entry{IconId::Zoom, "zoom"},
    Entry{IconId::Fit, "fit"},
    Entry{IconId::Display, "display"},
    Entry{IconId::Grid, "grid"},
    Entry{IconId::Camera, "camera"},
    Entry{IconId::Sketch, "sketch"},
    Entry{IconId::FinishSketch, "finish-sketch"},
    Entry{IconId::Line, "line"},
    Entry{IconId::Rectangle, "rectangle"},
    Entry{IconId::CenterRectangle, "center-rectangle"},
    Entry{IconId::Circle, "circle"},
    Entry{IconId::Arc, "arc"},
    Entry{IconId::Point, "point"},
    Entry{IconId::Dimension, "dimension"},
    Entry{IconId::Construction, "construction"},
    Entry{IconId::LookAt, "look-at"},
    Entry{IconId::Coincident, "coincident"},
    Entry{IconId::Horizontal, "horizontal"},
    Entry{IconId::Vertical, "vertical"},
    Entry{IconId::HorizontalVertical, "horizontal-vertical"},
    Entry{IconId::Parallel, "parallel"},
    Entry{IconId::Perpendicular, "perpendicular"},
    Entry{IconId::Tangent, "tangent"},
    Entry{IconId::Equal, "equal"},
    Entry{IconId::Midpoint, "midpoint"},
    Entry{IconId::Concentric, "concentric"},
    Entry{IconId::Fix, "fix"},
    Entry{IconId::Symmetric, "symmetric"},
    Entry{IconId::Extrude, "extrude"},
    Entry{IconId::Fillet, "fillet"},
    Entry{IconId::Chamfer, "chamfer"},
    Entry{IconId::Hole, "hole"},
    Entry{IconId::Combine, "combine"},
    Entry{IconId::Plane, "plane"},
    Entry{IconId::Section, "section"},
    Entry{IconId::Measure, "measure"},
    Entry{IconId::Overhang, "overhang"},
    Entry{IconId::Undo, "undo"},
    Entry{IconId::Redo, "redo"},
    Entry{IconId::Save, "save"},
    Entry{IconId::Open, "open"},
    Entry{IconId::New, "new"},
    Entry{IconId::ExportStl, "export-stl"},
    Entry{IconId::ExportStep, "export-step"},
    Entry{IconId::Print3D, "print-3d"},
    Entry{IconId::Eye, "eye"},
    Entry{IconId::EyeOff, "eye-off"},
    Entry{IconId::Body, "body"},
    Entry{IconId::SketchNode, "sketch-node"},
    Entry{IconId::PlaneNode, "plane-node"},
    Entry{IconId::Folder, "folder"},
    Entry{IconId::Warning, "warning"},
    Entry{IconId::Error, "error"},
    Entry{IconId::TimelineFirst, "timeline-first"},
    Entry{IconId::TimelineBack, "timeline-back"},
    Entry{IconId::TimelineForward, "timeline-forward"},
    Entry{IconId::TimelineLast, "timeline-last"},
    Entry{IconId::Origin, "origin"},
    Entry{IconId::Flip, "flip"},
    Entry{IconId::Settings, "settings"},
    Entry{IconId::McpServer, "mcp-server"},
    Entry{IconId::Delete, "delete"},
    Entry{IconId::Repeat, "repeat"},
};

// The SVG source with the accent colour swapped in.
QByteArray svgSource(IconId id, const QColor &accent) {
    static QHash<int, QByteArray> sources;
    auto it = sources.find(int(id));
    if(it == sources.end()) {
        QFile f(QStringLiteral(":/icons/%1.svg").arg(iconName(id)));
        it = sources.insert(int(id), f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray());
    }
    QByteArray svg = *it;
    if(accent != kIconAccent) {
        const QByteArray to = accent.name(QColor::HexRgb).toLatin1();
        svg.replace(kIconAccent.name(QColor::HexRgb).toLatin1(), to);
        svg.replace(kIconAccent.name(QColor::HexRgb).toUpper().toLatin1(), to);
    }
    return svg;
}

QImage render(const QByteArray &svg, QSize px) {
    QImage img(px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QSvgRenderer r(svg);
    if(!r.isValid()) return img;
    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    // Square icons, centred in non-square requests.
    const int side = std::min(px.width(), px.height());
    r.render(&p, QRectF((px.width() - side) / 2.0, (px.height() - side) / 2.0, side, side));
    return img;
}

// Disabled look: desaturated and faded.
QImage disabled(QImage img) {
    img = img.convertToFormat(QImage::Format_ARGB32);
    for(int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for(int x = 0; x < img.width(); ++x) {
            const QRgb c = line[x];
            const int g = (qRed(c) * 11 + qGreen(c) * 16 + qBlue(c) * 5) / 32;
            const int v = 150 + g * 100 / 255; // lift towards a light grey
            line[x] = qRgba(v, v, v, qAlpha(c) * 45 / 100);
        }
    }
    return img;
}

class SvgIconEngine : public QIconEngine {
public:
    SvgIconEngine(IconId id, QColor accent) : m_id(id), m_accent(accent) {}

    QIconEngine *clone() const override { return new SvgIconEngine(m_id, m_accent); }
    QString key() const override { return QStringLiteral("cadly-svg"); }

    QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override {
        return {QSize(16, 16), QSize(24, 24), QSize(32, 32), QSize(64, 64), QSize(128, 128)};
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QPixmap pm = scaledPixmap(rect.size(), mode, state, dpr);
        painter->drawPixmap(rect, pm);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State, qreal scale) override {
        const QSize px = (QSizeF(size) * scale).toSize();
        if(px.isEmpty()) return {};
        const QString cacheKey = QStringLiteral("cadly-icon-%1-%2-%3x%4-%5")
                                     .arg(int(m_id))
                                     .arg(m_accent.rgba())
                                     .arg(px.width())
                                     .arg(px.height())
                                     .arg(int(mode));
        QPixmap pm;
        if(!QPixmapCache::find(cacheKey, &pm)) {
            QImage img = render(svgSource(m_id, m_accent), px);
            if(mode == QIcon::Disabled) img = disabled(img);
            pm = QPixmap::fromImage(img);
            QPixmapCache::insert(cacheKey, pm);
        }
        pm.setDevicePixelRatio(scale);
        return pm;
    }

private:
    IconId m_id;
    QColor m_accent;
};

} // namespace

QString iconName(IconId id) {
    for(const Entry &e : kIcons)
        if(e.id == id) return QString::fromLatin1(e.name);
    return {};
}

std::vector<IconId> allIcons() {
    std::vector<IconId> out;
    for(const Entry &e : kIcons) out.push_back(e.id);
    return out;
}

QImage iconImage(IconId id, int size, const QColor &accent) { return render(svgSource(id, accent), QSize(size, size)); }

void paintGlyphChip(QPainter &p, const QRectF &r, const QIcon &ic, const QColor &border, bool hot) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(15, 30, 55, 45));
    p.drawRoundedRect(r.translated(0, 1.2).adjusted(-0.5, 0, 0.5, 0.5), 4.5, 4.5);
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, hot ? QColor(236, 244, 255) : QColor(255, 255, 255, 245));
    g.setColorAt(1, hot ? QColor(212, 229, 252) : QColor(236, 241, 247, 245));
    p.setPen(QPen(border, 1.0));
    p.setBrush(g);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    ic.paint(&p, r.adjusted(2, 2, -2, -2).toAlignedRect());
    p.restore();
}

QIcon icon(IconId id, const QColor &accent) {
    static QHash<QString, QIcon> cache;
    const QString key = QString::number(int(id)) + accent.name(QColor::HexArgb);
    auto it = cache.constFind(key);
    if(it != cache.constEnd()) return *it;
    QIcon ic(new SvgIconEngine(id, accent));
    cache.insert(key, ic);
    return ic;
}

} // namespace cadly
