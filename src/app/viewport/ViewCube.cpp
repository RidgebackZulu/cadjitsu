#include "viewport/ViewCube.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <array>
#include <cmath>

namespace cadjitsu {

namespace {

constexpr float kProjExtent = 2.2f; // half size of the ortho view around the unit cube
constexpr float kBevel = 0.78f;     // flat faces span +-kBevel; edges and corners are chamfered beyond

// Compass ring under the cube.
constexpr float kRingZ = -1.3f, kRingIn = 1.38f, kRingOut = 1.78f;

struct FaceDef {
    QVector3D normal, uAxis, vAxis; // v points "up" on the label
    const char *label;
};

// Order matches the atlas tiles.
const std::array<FaceDef, 6> kFaces = {{
    {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, "TOP"},
    {{0, 0, -1}, {1, 0, 0}, {0, -1, 0}, "BOTTOM"},
    {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, "FRONT"},
    {{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, "BACK"},
    {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, "RIGHT"},
    {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, "LEFT"},
}};

// Atlas tiles: 6 faces, a blank one (bevels), then the compass letters.
constexpr int kBlankTile = 6;
constexpr int kLetterTile = 7; // N, E, S, W
constexpr int kTiles = 11;

enum Material : int { Cube = 0, Ring = 1, Tick = 2, Letter = 3, AxisX = 4, AxisY = 5, AxisZ = 6 };

struct Builder {
    std::vector<float> &v;
    std::vector<uint16_t> &idx;

    uint16_t vertex(QVector3D p, QVector3D n, float u, float w, QVector3D region, Material m) {
        const uint16_t i = uint16_t(v.size() / ViewCube::kFloatsPerVertex);
        v.insert(v.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), u, w, region.x(), region.y(), region.z(),
                           float(m)});
        return i;
    }
    void tri(uint16_t a, uint16_t b, uint16_t c) { idx.insert(idx.end(), {a, b, c}); }
    void quad(uint16_t a, uint16_t b, uint16_t c, uint16_t d) { idx.insert(idx.end(), {a, b, c, a, c, d}); }

    // A flat quad (p0..p3) with one material and region, sampling the centre of `tile`.
    void flat(const std::array<QVector3D, 4> &p, QVector3D n, QVector3D region, Material m, int tile = kBlankTile) {
        const float u = (float(tile) + 0.5f) / kTiles;
        quad(vertex(p[0], n, u, 0.5f, region, m), vertex(p[1], n, u, 0.5f, region, m),
             vertex(p[2], n, u, 0.5f, region, m), vertex(p[3], n, u, 0.5f, region, m));
    }

    // A box (thin prism) for the axis triad.
    void prism(QVector3D from, QVector3D to, float half, Material m) {
        const QVector3D d = (to - from).normalized();
        QVector3D a = std::fabs(d.z()) < 0.9f ? QVector3D(0, 0, 1) : QVector3D(1, 0, 0);
        const QVector3D s = QVector3D::crossProduct(d, a).normalized() * half;
        const QVector3D t = QVector3D::crossProduct(d, s).normalized() * half;
        const std::array<QVector3D, 4> ring = {s + t, -s + t, -s - t, s - t};
        for(int k = 0; k < 4; ++k) {
            const QVector3D r0 = ring[size_t(k)], r1 = ring[size_t((k + 1) % 4)];
            flat({from + r0, from + r1, to + r1, to + r0}, (r0 + r1).normalized(), {}, m);
        }
        flat({to + ring[0], to + ring[1], to + ring[2], to + ring[3]}, d, {}, m);
    }
};

} // namespace

QRect ViewCube::rect(QSize viewport) {
    return QRect(viewport.width() - kSize - kMargin, kMargin, kSize, kSize);
}

QPointF ViewCube::centre(QSize viewport) { return QRectF(rect(viewport)).center(); }

double ViewCube::pixelsPerUnit() { return kSize / (2.0 * kProjExtent); }

QMatrix4x4 ViewCube::viewRotation(const QQuaternion &cameraRotation) {
    QMatrix4x4 m;
    m.rotate(cameraRotation.inverted());
    return m;
}

QMatrix4x4 ViewCube::viewProjection(const QQuaternion &cameraRotation) {
    QMatrix4x4 proj;
    proj.ortho(-kProjExtent, kProjExtent, -kProjExtent, kProjExtent, -10.0f, 10.0f);
    return proj * viewRotation(cameraRotation);
}

std::optional<QVector3D> ViewCube::hitTest(QPointF px, QSize viewport, const QQuaternion &cameraRotation) {
    const QRect r = rect(viewport);
    if(!r.contains(px.toPoint())) return std::nullopt;
    const float nx = float((px.x() - r.left()) / r.width()) * 2.0f - 1.0f;
    const float ny = 1.0f - float((px.y() - r.top()) / r.height()) * 2.0f;
    // Ray in cube space: from in front of the cube along the camera's forward axis.
    const QVector3D origin = cameraRotation.rotatedVector(QVector3D(nx * kProjExtent, ny * kProjExtent, 5.0f));
    const QVector3D dir = cameraRotation.rotatedVector(QVector3D(0, 0, -1));
    // The chamfered cube is the intersection of 26 half-spaces n.p <= d, one
    // per region (faces, edges, corners). The ray enters through exactly the
    // region it hits.
    float tEnter = -1e9f, tExit = 1e9f;
    QVector3D entered;
    for(int x = -1; x <= 1; ++x)
        for(int y = -1; y <= 1; ++y)
            for(int z = -1; z <= 1; ++z) {
                const int nonZero = (x != 0) + (y != 0) + (z != 0);
                if(nonZero == 0) continue;
                const QVector3D n{float(x), float(y), float(z)};
                const float d = 1.0f + float(nonZero - 1) * kBevel;
                const float denom = QVector3D::dotProduct(n, dir);
                const float num = d - QVector3D::dotProduct(n, origin);
                if(std::fabs(denom) < 1e-9f) {
                    if(num < 0) return std::nullopt;
                    continue;
                }
                const float t = num / denom;
                if(denom < 0) {
                    if(t > tEnter) {
                        tEnter = t;
                        entered = n;
                    }
                } else {
                    tExit = std::min(tExit, t);
                }
            }
    if(tEnter > tExit || entered.isNull()) return std::nullopt;
    return entered;
}

QQuaternion ViewCube::orientationForRegion(const QVector3D &dir) {
    const QVector3D viewDir = (-dir).normalized();
    QVector3D up(0, 0, 1);
    if(std::fabs(dir.x()) < 0.5f && std::fabs(dir.y()) < 0.5f) up = dir.z() > 0 ? QVector3D(0, 1, 0) : QVector3D(0, -1, 0);
    const QVector3D z = -viewDir;
    const QVector3D x = QVector3D::crossProduct(up, z).normalized();
    const QVector3D y = QVector3D::crossProduct(z, x);
    return QQuaternion::fromAxes(x, y, z).normalized();
}

void ViewCube::mesh(std::vector<float> &vertices, std::vector<uint16_t> &indices) {
    vertices.clear();
    indices.clear();
    Builder b{vertices, indices};
    const float k = kBevel;

    // Faces: flat squares of +-k carrying the labels.
    for(size_t f = 0; f < kFaces.size(); ++f) {
        const FaceDef &fd = kFaces[f];
        uint16_t id[4];
        const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for(int c = 0; c < 4; ++c) {
            const float a = corners[c][0], bb = corners[c][1];
            const QVector3D p = fd.normal + fd.uAxis * (a * k) + fd.vAxis * (bb * k);
            const float u = (float(f) + 0.02f + (a + 1.0f) * 0.48f) / kTiles;
            const float v = 1.0f - (0.02f + (bb + 1.0f) * 0.48f);
            id[c] = b.vertex(p, fd.normal, u, v, fd.normal, Cube);
        }
        b.quad(id[0], id[1], id[2], id[3]);
    }
    // Edges: bevel strips between two faces.
    for(int i = 0; i < 3; ++i)
        for(int j = i + 1; j < 3; ++j) {
            const int l = 3 - i - j;
            for(int si : {-1, 1})
                for(int sj : {-1, 1}) {
                    auto P = [&](float ai, float aj, float al) {
                        QVector3D p;
                        p[i] = ai * float(si);
                        p[j] = aj * float(sj);
                        p[l] = al;
                        return p;
                    };
                    QVector3D region;
                    region[i] = float(si);
                    region[j] = float(sj);
                    b.flat({P(1, k, -k), P(1, k, k), P(k, 1, k), P(k, 1, -k)}, region.normalized(), region, Cube);
                }
        }
    // Corners: small triangles.
    for(int sx : {-1, 1})
        for(int sy : {-1, 1})
            for(int sz : {-1, 1}) {
                const QVector3D s{float(sx), float(sy), float(sz)};
                const QVector3D n = s.normalized();
                const float u = (kBlankTile + 0.5f) / kTiles;
                b.tri(b.vertex(QVector3D(1, k, k) * s, n, u, 0.5f, s, Cube),
                      b.vertex(QVector3D(k, 1, k) * s, n, u, 0.5f, s, Cube),
                      b.vertex(QVector3D(k, k, 1) * s, n, u, 0.5f, s, Cube));
            }

    // Compass ring under the cube, with ticks and N/E/S/W (north is +Y, BACK).
    const QVector3D up(0, 0, 1);
    const int segs = 96;
    for(int s = 0; s < segs; ++s) {
        const float a0 = 2.0f * float(M_PI) * s / segs, a1 = 2.0f * float(M_PI) * (s + 1) / segs;
        auto at = [&](float r, float a) { return QVector3D(r * std::cos(a), r * std::sin(a), kRingZ); };
        b.flat({at(kRingIn, a0), at(kRingOut, a0), at(kRingOut, a1), at(kRingIn, a1)}, up, {}, Ring);
    }
    const float letterHalf = 0.24f;
    for(int q = 0; q < 4; ++q) {
        // N (+Y), E (+X), S (-Y), W (-X).
        const float ang = float(M_PI) / 2.0f - float(q) * float(M_PI) / 2.0f;
        const QVector3D radial(std::cos(ang), std::sin(ang), 0);
        const QVector3D side(-radial.y(), radial.x(), 0);
        const QVector3D c = radial * ((kRingIn + kRingOut) / 2.0f) + QVector3D(0, 0, kRingZ + 0.004f);
        // Letter upright pointing outwards.
        const float u0 = float(kLetterTile + q) / kTiles, u1 = float(kLetterTile + q + 1) / kTiles;
        const QVector3D r = radial * letterHalf, sd = side * letterHalf;
        b.quad(b.vertex(c - sd - r, up, u1, 1.0f, {}, Letter), b.vertex(c + sd - r, up, u0, 1.0f, {}, Letter),
               b.vertex(c + sd + r, up, u0, 0.0f, {}, Letter), b.vertex(c - sd + r, up, u1, 0.0f, {}, Letter));
        // Tick marks halfway between the letters.
        const float ta = ang - float(M_PI) / 4.0f;
        const QVector3D tr(std::cos(ta), std::sin(ta), 0), ts(-tr.y(), tr.x(), 0);
        const QVector3D tc = tr * ((kRingIn + kRingOut) / 2.0f) + QVector3D(0, 0, kRingZ + 0.004f);
        b.flat({tc - ts * 0.025f - tr * 0.11f, tc + ts * 0.025f - tr * 0.11f, tc + ts * 0.025f + tr * 0.11f,
                tc - ts * 0.025f + tr * 0.11f},
               up, {}, Tick);
    }

    // Axis triad at the cube's origin corner (left, front, bottom).
    const QVector3D o(-1.16f, -1.16f, -1.16f);
    const float len = 1.05f, half = 0.045f;
    b.prism(o, o + QVector3D(len, 0, 0), half, AxisX);
    b.prism(o, o + QVector3D(0, len, 0), half, AxisY);
    b.prism(o, o + QVector3D(0, 0, len), half, AxisZ);
}

QImage ViewCube::labelAtlas(qreal dpr) {
    const int tile = int(128 * dpr);
    QImage img(tile * kTiles, tile, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font;
    font.setBold(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, tile * 0.006);
    for(size_t f = 0; f < kFaces.size(); ++f) {
        const QRectF r(int(f) * tile, 0, tile, tile);
        // A hairline where the face meets its bevels, and a soft inner rim.
        p.setPen(QPen(QColor(128, 140, 158, 230), tile * 0.018));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(tile * 0.012, tile * 0.012, -tile * 0.012, -tile * 0.012));
        p.setPen(QPen(QColor(255, 255, 255, 170), tile * 0.02));
        p.drawRect(r.adjusted(tile * 0.04, tile * 0.04, -tile * 0.04, -tile * 0.04));
        const QString text = QString::fromLatin1(kFaces[f].label);
        font.setPixelSize(int(tile * (text.size() > 5 ? 0.165 : 0.2)));
        p.setFont(font);
        // Embossed: a white highlight under dark text.
        p.setPen(QColor(255, 255, 255, 220));
        p.drawText(r.translated(0, tile * 0.012), Qt::AlignCenter, text);
        p.setPen(QColor(52, 62, 78));
        p.drawText(r, Qt::AlignCenter, text);
    }
    // Compass letters (alpha only; the shader tints them).
    font.setPixelSize(int(tile * 0.62));
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    p.setFont(font);
    const char *letters[] = {"N", "E", "S", "W"};
    for(int q = 0; q < 4; ++q) {
        const QRectF r((kLetterTile + q) * tile, 0, tile, tile);
        p.setPen(QColor(40, 52, 70));
        p.drawText(r, Qt::AlignCenter, QString::fromLatin1(letters[q]));
    }
    p.end();
    return img;
}

} // namespace cadjitsu
