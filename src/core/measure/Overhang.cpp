#include "measure/Overhang.h"

#include <algorithm>
#include <cmath>

namespace cad {

namespace {
constexpr double kDeg = 3.14159265358979323846 / 180.0;
}

OverhangKind classifyOverhang(double nx, double ny, double nz, bool onPlate, const OverhangOptions &o) {
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if(len < 1e-12) return OverhangKind::Ok;
    nz /= len;
    if(nz >= 0.0) return OverhangKind::Ok; // up-facing or vertical walls print on what is below
    // How far the face leans from vertical (0 = a wall, 90 = facing straight down).
    const double lean = std::asin(std::min(1.0, -nz)) / kDeg;
    if(lean >= 90.0 - o.flatBand) return onPlate ? OverhangKind::Plate : OverhangKind::Bridge;
    if(lean > o.threshold) return OverhangKind::Overhang;
    if(lean > o.threshold - o.nearBand) return OverhangKind::Near;
    return OverhangKind::Ok;
}

OverhangReport analyzeOverhangs(const MeshData &m, const OverhangOptions &o, bool wantTriangles) {
    OverhangReport r;
    const size_t nTri = m.triangleCount();
    for(size_t t = 0; t < nTri; ++t) {
        const uint32_t ia = m.indices[3 * t], ib = m.indices[3 * t + 1], ic = m.indices[3 * t + 2];
        const float *a = &m.positions[3 * ia], *b = &m.positions[3 * ib], *c = &m.positions[3 * ic];
        const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const double twiceArea = std::sqrt(nx * nx + ny * ny + nz * nz);
        if(twiceArea < 1e-14) continue;
        // Face the same way as the shading normals (outwards).
        if(!m.normals.empty()) {
            const float *na = &m.normals[3 * ia], *nb = &m.normals[3 * ib], *nc = &m.normals[3 * ic];
            const double sx = na[0] + nb[0] + nc[0], sy = na[1] + nb[1] + nc[1], sz = na[2] + nb[2] + nc[2];
            if(nx * sx + ny * sy + nz * sz < 0) {
                nx = -nx;
                ny = -ny;
                nz = -nz;
            }
        }
        const bool onPlate = std::fabs(a[2] - o.plateZ) < 0.01 && std::fabs(b[2] - o.plateZ) < 0.01 &&
                             std::fabs(c[2] - o.plateZ) < 0.01;
        const OverhangKind k = classifyOverhang(nx, ny, nz, onPlate, o);
        const double area = twiceArea / 2.0;
        r.area[size_t(k)] += area;
        if((k == OverhangKind::Overhang || k == OverhangKind::Bridge) && t < m.triangleFace.size())
            r.faceOverhangArea[int(m.triangleFace[t])] += area;
        if(wantTriangles) {
            auto &out = r.triangles[size_t(k)];
            out.insert(out.end(), {a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]});
        }
    }
    return r;
}

} // namespace cad
