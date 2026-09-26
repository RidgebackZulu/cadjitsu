#include "mesh/MeshValidator.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <unordered_map>

namespace cad {

namespace {

using V3 = std::array<double, 3>;

V3 sub(const V3 &a, const V3 &b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 cross(const V3 &a, const V3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const V3 &a, const V3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double norm(const V3 &a) { return std::sqrt(dot(a, a)); }

} // namespace

std::string MeshReport::summary() const {
    char buf[256];
    std::snprintf(buf, sizeof buf, "%zu triangles, %zu vertices, %zu shell%s, volume %.3f mm^3%s", triangles, vertices,
                  shells, shells == 1 ? "" : "s", volume, ok ? " - watertight" : "");
    std::string s = buf;
    for(const auto &p : problems) s += "\n  - " + p;
    return s;
}

MeshReport validateMesh(const TriMesh &mesh, double expectedVolume, double volumeTolerance) {
    MeshReport r;
    r.vertices = mesh.vertices.size();
    r.triangles = mesh.triangles.size();
    if(mesh.triangles.empty()) {
        r.problems.push_back("the mesh is empty");
        return r;
    }

    // Scale for degeneracy tests.
    double scale = 0.0;
    for(const auto &v : mesh.vertices) {
        for(double c : v) {
            if(!std::isfinite(c)) {
                r.problems.push_back("the mesh contains invalid (NaN or infinite) coordinates");
                return r;
            }
            scale = std::max(scale, std::fabs(c));
        }
    }
    scale = std::max(scale, 1.0);
    const double areaEps = 1e-14 * scale * scale;

    // Directed edge usage: key (min, max) -> count of min->max and max->min.
    struct Use {
        uint32_t fwd = 0, bwd = 0;
    };
    std::unordered_map<uint64_t, Use> edgesUse;
    edgesUse.reserve(mesh.triangles.size() * 2);
    for(const auto &t : mesh.triangles) {
        const V3 &a = mesh.vertices[t[0]], &b = mesh.vertices[t[1]], &c = mesh.vertices[t[2]];
        const V3 n = cross(sub(b, a), sub(c, a));
        const double area2 = norm(n);
        if(t[0] == t[1] || t[1] == t[2] || t[0] == t[2] || area2 <= areaEps) ++r.degenerateTriangles;
        r.area += 0.5 * area2;
        r.volume += dot(a, cross(b, c)) / 6.0;
        for(int k = 0; k < 3; ++k) {
            const uint32_t u = t[k], v = t[(k + 1) % 3];
            const uint64_t key = u < v ? (uint64_t(u) << 32 | v) : (uint64_t(v) << 32 | u);
            Use &use = edgesUse[key];
            if(u < v) ++use.fwd;
            else ++use.bwd;
        }
    }
    for(const auto &kv : edgesUse) {
        const uint32_t total = kv.second.fwd + kv.second.bwd;
        if(total == 1) ++r.boundaryEdges;
        else if(total > 2) ++r.nonManifoldEdges;
        else if(kv.second.fwd != 1) ++r.flippedEdges;
    }

    // Shells: connected components over shared vertices.
    std::vector<uint32_t> parent(mesh.vertices.size());
    std::iota(parent.begin(), parent.end(), 0u);
    std::function<uint32_t(uint32_t)> root = [&](uint32_t x) {
        while(parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    for(const auto &t : mesh.triangles) {
        parent[root(t[0])] = root(t[1]);
        parent[root(t[1])] = root(t[2]);
    }
    std::unordered_map<uint32_t, int> roots;
    for(const auto &t : mesh.triangles) roots[root(t[0])] = 1;
    r.shells = roots.size();

    r.watertight = r.boundaryEdges == 0 && r.nonManifoldEdges == 0 && r.flippedEdges == 0;
    if(r.boundaryEdges) r.problems.push_back(std::to_string(r.boundaryEdges) + " open edge(s): the mesh has holes");
    if(r.nonManifoldEdges)
        r.problems.push_back(std::to_string(r.nonManifoldEdges) + " non-manifold edge(s) shared by 3+ triangles");
    if(r.flippedEdges)
        r.problems.push_back(std::to_string(r.flippedEdges) + " edge(s) with inconsistent triangle winding");
    if(r.degenerateTriangles)
        r.problems.push_back(std::to_string(r.degenerateTriangles) + " degenerate (zero-area) triangle(s)");
    if(r.volume <= 0) r.problems.push_back("the mesh is inside-out (negative volume)");
    const double volTol = volumeTolerance > 0 ? volumeTolerance : 0.02 * expectedVolume;
    if(expectedVolume > 0 && std::fabs(r.volume - expectedVolume) > volTol) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "mesh volume %.4g differs from the solid volume %.4g", r.volume,
                      expectedVolume);
        r.problems.push_back(buf);
    }
    r.ok = r.problems.empty();
    return r;
}

} // namespace cad
