#include "viewport/Picker.h"

#include <QLineF>

#include <cmath>

namespace cadly {

namespace {

inline QVector3D vtx(const std::vector<float> &a, size_t i) { return QVector3D(a[3 * i], a[3 * i + 1], a[3 * i + 2]); }

bool clipped(const std::optional<QVector4D> &plane, const QVector3D &p) {
    return plane && QVector3D::dotProduct(plane->toVector3D(), p) + plane->w() > 0.0f;
}

// Möller-Trumbore; returns t along the ray or a negative value.
float rayTriangle(const QVector3D &o, const QVector3D &d, const QVector3D &a, const QVector3D &b, const QVector3D &c) {
    const QVector3D e1 = b - a, e2 = c - a;
    const QVector3D p = QVector3D::crossProduct(d, e2);
    const float det = QVector3D::dotProduct(e1, p);
    if(std::fabs(det) < 1e-12f) return -1.0f;
    const float inv = 1.0f / det;
    const QVector3D s = o - a;
    const float u = QVector3D::dotProduct(s, p) * inv;
    if(u < 0.0f || u > 1.0f) return -1.0f;
    const QVector3D q = QVector3D::crossProduct(s, e1);
    const float v = QVector3D::dotProduct(d, q) * inv;
    if(v < 0.0f || u + v > 1.0f) return -1.0f;
    return QVector3D::dotProduct(e2, q) * inv;
}

bool rayBox(const QVector3D &o, const QVector3D &d, const Box3 &b) {
    float tmin = -1e30f, tmax = 1e30f;
    for(int k = 0; k < 3; ++k) {
        const float ok = o[k], dk = d[k];
        if(std::fabs(dk) < 1e-12f) {
            if(ok < b.min[k] || ok > b.max[k]) return false;
            continue;
        }
        float t1 = (b.min[k] - ok) / dk, t2 = (b.max[k] - ok) / dk;
        if(t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if(tmin > tmax) return false;
    }
    return tmax >= 0.0f;
}

struct FaceHit {
    float t = std::numeric_limits<float>::infinity();
    const PickTarget *target = nullptr;
    int face = 0;
};

FaceHit nearestFace(const Camera &cam, QPointF px, const std::vector<PickTarget> &targets,
                    const std::optional<QVector4D> &clip) {
    QVector3D o, d;
    cam.ray(px, o, d);
    FaceHit best;
    for(const auto &t : targets) {
        if(!t.mesh) continue;
        const cad::MeshData &m = *t.mesh;
        Box3 bb = meshBounds(m);
        const float pad = std::max(bb.radius() * 1e-3f, 1e-3f);
        bb.min -= QVector3D(pad, pad, pad);
        bb.max += QVector3D(pad, pad, pad);
        if(!rayBox(o, d, bb)) continue;
        const size_t ntri = m.triangleCount();
        for(size_t i = 0; i < ntri; ++i) {
            const QVector3D a = vtx(m.positions, m.indices[3 * i]);
            const QVector3D b = vtx(m.positions, m.indices[3 * i + 1]);
            const QVector3D c = vtx(m.positions, m.indices[3 * i + 2]);
            const float tt = rayTriangle(o, d, a, b, c);
            if(tt <= 0.0f || tt >= best.t) continue;
            if(clipped(clip, o + d * tt)) continue;
            best.t = tt;
            best.target = &t;
            best.face = i < m.triangleFace.size() ? int(m.triangleFace[i]) : 0;
        }
    }
    return best;
}

// Caches the camera matrices for projecting many points.
struct Projector {
    QMatrix4x4 vp;
    QVector3D eye, fwd;
    float w, h;
    explicit Projector(const Camera &c)
        : vp(c.viewProjection()), eye(c.eye()), fwd(c.forward()), w(float(c.viewport.width())),
          h(float(c.viewport.height())) {}
    QPointF operator()(const QVector3D &p, float *depth = nullptr) const {
        const QVector4D clip = vp * QVector4D(p, 1.0f);
        if(depth) *depth = QVector3D::dotProduct(p - eye, fwd);
        const float iw = std::fabs(clip.w()) > 1e-12f ? 1.0f / clip.w() : 0.0f;
        return QPointF((clip.x() * iw + 1.0f) * 0.5f * w, (1.0f - clip.y() * iw) * 0.5f * h);
    }
};

float segmentDistance2D(QPointF p, QPointF a, QPointF b, float &u) {
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    double s = len2 > 0 ? ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2 : 0.0;
    s = std::clamp(s, 0.0, 1.0);
    u = float(s);
    const double cx = a.x() + s * dx - p.x(), cy = a.y() + s * dy - p.y();
    return float(std::sqrt(cx * cx + cy * cy));
}

} // namespace

Box3 meshBounds(const cad::MeshData &m) {
    Box3 b;
    if(m.vertexCount() == 0 && m.edgePoints.empty()) return b;
    b.add(QVector3D(m.bboxMin[0], m.bboxMin[1], m.bboxMin[2]));
    b.add(QVector3D(m.bboxMax[0], m.bboxMax[1], m.bboxMax[2]));
    return b;
}

std::optional<QVector3D> raycast(const Camera &camera, QPointF px, const std::vector<PickTarget> &targets,
                                 const std::optional<QVector4D> &clipPlane) {
    const FaceHit h = nearestFace(camera, px, targets, clipPlane);
    if(!h.target) return std::nullopt;
    QVector3D o, d;
    camera.ray(px, o, d);
    return o + d * h.t;
}

PickHit pick(const Camera &cam, QPointF px, const std::vector<PickTarget> &targets, const PickOptions &opt) {
    QVector3D o, d;
    cam.ray(px, o, d);
    const FaceHit face = nearestFace(cam, px, targets, opt.clipPlane);
    const float faceDepth = face.target ? cam.depthOf(o + d * face.t) : std::numeric_limits<float>::infinity();

    // An edge or vertex is visible if it is not behind the first face hit
    // (with a tolerance that scales with depth, since it lies on that face).
    auto visible = [&](const QVector3D &p) {
        if(clipped(opt.clipPlane, p)) return false;
        const float depth = cam.depthOf(p);
        if(depth < cam.nearPlane && !cam.orthographic) return false;
        const float tol = std::max(cam.unitsPerPixel(std::max(depth, 1e-3f)) * 3.0f, 1e-4f * std::fabs(depth));
        return depth <= faceDepth + tol;
    };

    const Projector proj(cam);
    PickHit best;
    best.screen = px;
    if(opt.vertices) {
        for(const auto &t : targets) {
            if(!t.mesh) continue;
            const auto &vp = t.mesh->vertexPoints;
            for(size_t i = 0; i < vp.size() / 3; ++i) {
                const QVector3D p = vtx(vp, i);
                const float dist = float(QLineF(proj(p), px).length());
                if(dist > opt.vertexTolerance) continue;
                if(best.valid() && dist >= best.screenDistance) continue;
                if(!visible(p)) continue;
                best.kind = PickHit::Kind::Vertex;
                best.body = t.body;
                best.index = int(i) + 1;
                best.point = p;
                best.screenDistance = dist;
            }
        }
        if(best.valid()) return best;
    }
    if(opt.edges) {
        for(const auto &t : targets) {
            if(!t.mesh) continue;
            const cad::MeshData &m = *t.mesh;
            for(size_t e = 0; e < m.edgeRanges.size(); ++e) {
                const auto &r = m.edgeRanges[e];
                for(uint32_t k = 0; k + 1 < r.count; ++k) {
                    const QVector3D a = vtx(m.edgePoints, r.first + k), b = vtx(m.edgePoints, r.first + k + 1);
                    float da, db;
                    const QPointF pa = proj(a, &da), pb = proj(b, &db);
                    if(da < cam.nearPlane && db < cam.nearPlane && !cam.orthographic) continue;
                    float u;
                    const float dist = segmentDistance2D(px, pa, pb, u);
                    if(dist > opt.edgeTolerance) continue;
                    if(best.valid() && dist >= best.screenDistance) continue;
                    // Perspective-correct: the screen parameter u maps to 3D parameter t.
                    float s3 = u;
                    if(!cam.orthographic) {
                        const float den = u * da + (1.0f - u) * db;
                        if(std::fabs(den) > 1e-12f) s3 = u * da / den;
                    }
                    const QVector3D p = a + (b - a) * s3;
                    if(!visible(p)) continue;
                    best.kind = PickHit::Kind::Edge;
                    best.body = t.body;
                    best.index = int(e) + 1;
                    best.point = p;
                    best.screenDistance = dist;
                }
            }
        }
        if(best.valid()) return best;
    }
    if(opt.faces && face.target) {
        best.kind = PickHit::Kind::Face;
        best.body = face.target->body;
        best.index = face.face;
        best.point = o + d * face.t;
        best.rayT = face.t;
    }
    return best;
}

std::vector<cad::BodyId> bodiesInRect(const Camera &cam, const QRectF &rect, bool crossing,
                                      const std::vector<PickTarget> &targets) {
    std::vector<cad::BodyId> out;
    const Projector proj(cam);
    for(const auto &t : targets) {
        if(!t.mesh || t.mesh->vertexCount() == 0) continue;
        const cad::MeshData &m = *t.mesh;
        bool anyInside = false, allInside = true;
        for(size_t i = 0; i < m.vertexCount(); ++i) {
            const bool in = rect.contains(proj(vtx(m.positions, i)));
            anyInside |= in;
            allInside &= in;
            if(crossing && anyInside) break;
        }
        if(crossing) {
            // A body also crosses the window if the window is entirely over it.
            if(!anyInside) {
                const std::vector<PickTarget> one{t};
                anyInside = raycast(cam, rect.center(), one).has_value();
            }
            if(anyInside) out.push_back(t.body);
        } else if(allInside) {
            out.push_back(t.body);
        }
    }
    return out;
}

} // namespace cadly
