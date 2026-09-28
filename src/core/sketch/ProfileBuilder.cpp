#include "sketch/ProfileBuilder.h"

#include "sketch/Sketch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <numeric>
#include <unordered_map>

namespace cad {

namespace {

constexpr double kTwoPi = 2.0 * kPi;

// ---------------------------------------------------------------------------
// Segment geometry helpers

struct GSeg {
    int v0 = -1, v1 = -1;
    bool isArc = false;
    Vec2 c;
    double r = 0.0;
    double aStart = 0.0, sweep = 0.0; // arcs run CCW from aStart by sweep (> 0)
    std::string key;
    int curveId = 0;
    bool alive = true;
};

Vec2 polar(Vec2 c, double r, double a) { return {c.x + r * std::cos(a), c.y + r * std::sin(a)}; }

// Area contribution (shoelace / Green) of a CCW arc from angle s by sweep sw.
double arcAreaTerm(Vec2 c, double r, double s, double sw) {
    const double e = s + sw;
    return 0.5 * (r * r * sw + r * c.x * (std::sin(e) - std::sin(s)) - r * c.y * (std::cos(e) - std::cos(s)));
}

double lineAreaTerm(Vec2 a, Vec2 b) { return 0.5 * (a.x * b.y - b.x * a.y); }

// ---------------------------------------------------------------------------
// Vertex welding with a hash grid.

class Welder {
public:
    explicit Welder(double tol) : m_tol(tol), m_cell(tol * 4.0) {}

    int add(Vec2 p) {
        const int64_t cx = cell(p.x), cy = cell(p.y);
        for(int64_t dx = -1; dx <= 1; ++dx) {
            for(int64_t dy = -1; dy <= 1; ++dy) {
                auto it = m_grid.find(keyOf(cx + dx, cy + dy));
                if(it == m_grid.end()) continue;
                for(int idx : it->second)
                    if(distance(m_points[idx], p) <= m_tol) return idx;
            }
        }
        const int idx = int(m_points.size());
        m_points.push_back(p);
        m_grid[keyOf(cx, cy)].push_back(idx);
        return idx;
    }

    const std::vector<Vec2> &points() const { return m_points; }

private:
    int64_t cell(double v) const { return int64_t(std::floor(v / m_cell)); }
    static uint64_t keyOf(int64_t x, int64_t y) {
        return (uint64_t(x) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(y) + 0x632BE59BD9B4E019ull);
    }

    double m_tol, m_cell;
    std::vector<Vec2> m_points;
    std::unordered_map<uint64_t, std::vector<int>> m_grid;
};

// ---------------------------------------------------------------------------
// Curve parametrization: lines use t in [0,1]; arcs use the angle offset from
// a0 in [0, sweep]; circles use the absolute angle in [0, 2pi).

struct Hit {
    double param;
    Vec2 p;
};

bool angleOnCurve(const Curve2 &cv, double angle, double angTol, double &param) {
    if(cv.type == Curve2::Circle) {
        param = normAngle(angle);
        if(param > kTwoPi - 1e-12) param = 0.0;
        return true;
    }
    double off = normAngle(angle - cv.a0);
    if(off > kTwoPi - angTol) off -= kTwoPi; // just before the start
    if(off < -angTol || off > cv.sweep + angTol) return false;
    param = std::clamp(off, 0.0, cv.sweep);
    return true;
}

void intersectLineLine(const Curve2 &A, const Curve2 &B, double tol, std::vector<Hit> &ha,
                       std::vector<Hit> &hb) {
    const Vec2 r = A.p1 - A.p0, s = B.p1 - B.p0;
    const double lr = r.length(), ls = s.length();
    if(lr < tol || ls < tol) return;
    const double denom = r.cross(s);
    const Vec2 qp = B.p0 - A.p0;
    if(std::fabs(denom) < 1e-12 * lr * ls) {
        // Parallel: only collinear overlaps matter.
        if(std::fabs(qp.cross(r)) / lr > tol) return;
        auto onA = [&](Vec2 p) {
            const double t = (p - A.p0).dot(r) / (lr * lr);
            if(t > -tol / lr && t < 1.0 + tol / lr) ha.push_back({std::clamp(t, 0.0, 1.0), p});
        };
        auto onB = [&](Vec2 p) {
            const double u = (p - B.p0).dot(s) / (ls * ls);
            if(u > -tol / ls && u < 1.0 + tol / ls) hb.push_back({std::clamp(u, 0.0, 1.0), p});
        };
        onA(B.p0);
        onA(B.p1);
        onB(A.p0);
        onB(A.p1);
        return;
    }
    const double t = qp.cross(s) / denom;
    const double u = qp.cross(r) / denom;
    const double et = tol / lr, eu = tol / ls;
    if(t < -et || t > 1.0 + et || u < -eu || u > 1.0 + eu) return;
    const Vec2 p = A.p0 + r * std::clamp(t, 0.0, 1.0);
    ha.push_back({std::clamp(t, 0.0, 1.0), p});
    hb.push_back({std::clamp(u, 0.0, 1.0), p});
}

void intersectLineCircle(const Curve2 &L, const Curve2 &C, double tol, std::vector<Hit> &hl,
                         std::vector<Hit> &hc) {
    const Vec2 d = L.p1 - L.p0;
    const double len = d.length();
    if(len < tol || C.r < tol) return;
    const Vec2 u = d / len;
    // Closest approach of the infinite line to the centre.
    const double tc = (C.c - L.p0).dot(u);
    const Vec2 foot = L.p0 + u * tc;
    const double dist = distance(foot, C.c);
    if(dist > C.r + tol) return;
    std::vector<double> ts;
    if(dist > C.r - tol) {
        ts.push_back(tc); // tangent
    } else {
        const double h = std::sqrt(std::max(0.0, C.r * C.r - dist * dist));
        ts.push_back(tc - h);
        ts.push_back(tc + h);
    }
    const double angTol = tol / C.r;
    for(double t : ts) {
        if(t < -tol || t > len + tol) continue;
        const double tn = std::clamp(t / len, 0.0, 1.0);
        const Vec2 p = L.p0 + d * tn;
        double param;
        if(!angleOnCurve(C, (p - C.c).angle(), angTol, param)) continue;
        hl.push_back({tn, p});
        hc.push_back({param, p});
    }
}

void intersectCircleCircle(const Curve2 &A, const Curve2 &B, double tol, std::vector<Hit> &ha,
                           std::vector<Hit> &hb) {
    const double d = distance(A.c, B.c);
    if(d < tol) return; // concentric (identical circles are deduplicated later)
    if(d > A.r + B.r + tol || d < std::fabs(A.r - B.r) - tol) return;
    const double a = (A.r * A.r - B.r * B.r + d * d) / (2.0 * d);
    const double h2 = A.r * A.r - a * a;
    const Vec2 ex = (B.c - A.c) / d;
    const Vec2 base = A.c + ex * a;
    std::vector<Vec2> pts;
    if(h2 <= tol * tol) {
        pts.push_back(base);
    } else {
        const double h = std::sqrt(h2);
        pts.push_back(base + ex.perp() * h);
        pts.push_back(base - ex.perp() * h);
    }
    for(const Vec2 &p : pts) {
        double pa, pb;
        if(!angleOnCurve(A, (p - A.c).angle(), tol / A.r, pa)) continue;
        if(!angleOnCurve(B, (p - B.c).angle(), tol / B.r, pb)) continue;
        ha.push_back({pa, p});
        hb.push_back({pb, p});
    }
}

bool isRound(const Curve2 &c) { return c.type != Curve2::Line; }

void intersect(const Curve2 &A, const Curve2 &B, double tol, std::vector<Hit> &ha, std::vector<Hit> &hb) {
    if(!isRound(A) && !isRound(B)) intersectLineLine(A, B, tol, ha, hb);
    else if(!isRound(A)) intersectLineCircle(A, B, tol, ha, hb);
    else if(!isRound(B)) intersectLineCircle(B, A, tol, hb, ha);
    else intersectCircleCircle(A, B, tol, ha, hb);
}

struct Bounds {
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    void add(Vec2 p) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    bool overlaps(const Bounds &o, double tol) const {
        return minX <= o.maxX + tol && o.minX <= maxX + tol && minY <= o.maxY + tol && o.minY <= maxY + tol;
    }
};

Bounds curveBounds(const Curve2 &c) {
    Bounds b;
    if(c.type == Curve2::Line) {
        b.add(c.p0);
        b.add(c.p1);
    } else {
        b.add({c.c.x - c.r, c.c.y - c.r});
        b.add({c.c.x + c.r, c.c.y + c.r});
    }
    return b;
}

// ---------------------------------------------------------------------------
// Half-edge graph

struct HalfEdge {
    int seg;
    bool forward;
    int from, to;
    int64_t angleKey; // quantized outgoing tangent angle
    double curvature; // signed, positive = turning left
};

double segArea(const GSeg &s, bool forward, const std::vector<Vec2> &pts) {
    if(!s.isArc) return forward ? lineAreaTerm(pts[s.v0], pts[s.v1]) : lineAreaTerm(pts[s.v1], pts[s.v0]);
    const double a = arcAreaTerm(s.c, s.r, s.aStart, s.sweep);
    return forward ? a : -a;
}

ProfileSeg toProfileSeg(const GSeg &s, bool forward, const std::vector<Vec2> &pts) {
    ProfileSeg ps;
    ps.isArc = s.isArc;
    ps.p0 = forward ? pts[s.v0] : pts[s.v1];
    ps.p1 = forward ? pts[s.v1] : pts[s.v0];
    ps.c = s.c;
    ps.r = s.r;
    ps.ccw = forward;
    ps.key = s.key;
    ps.curveId = s.curveId;
    return ps;
}

Vec2 findInteriorPoint(const Profile &p) {
    std::vector<std::vector<Vec2>> polys;
    polys.push_back(p.outer.polygon());
    for(const auto &h : p.holes) polys.push_back(h.polygon());
    Bounds b;
    for(const Vec2 &v : polys[0]) b.add(v);

    Vec2 best = polys[0].empty() ? Vec2() : polys[0][0];
    double bestWidth = -1.0;
    const int N = 9;
    for(int k = 1; k <= N; ++k) {
        // Slightly irrational offsets avoid scanning exactly through vertices.
        const double f = (k + 0.1234567) / (N + 1.0);
        const double y = b.minY + (b.maxY - b.minY) * f;
        std::vector<double> xs;
        for(const auto &poly : polys) {
            const size_t n = poly.size();
            for(size_t i = 0; i < n; ++i) {
                const Vec2 a = poly[i], c = poly[(i + 1) % n];
                if((a.y > y) == (c.y > y)) continue;
                xs.push_back(a.x + (y - a.y) * (c.x - a.x) / (c.y - a.y));
            }
        }
        std::sort(xs.begin(), xs.end());
        for(size_t i = 0; i + 1 < xs.size(); i += 2) {
            const double w = xs[i + 1] - xs[i];
            if(w > bestWidth) {
                bestWidth = w;
                best = {0.5 * (xs[i] + xs[i + 1]), y};
            }
        }
    }
    return best;
}

} // namespace

// ---------------------------------------------------------------------------

Vec2 ProfileSeg::midpoint() const {
    if(!isArc) return (p0 + p1) * 0.5;
    const double as = (p0 - c).angle(), ae = (p1 - c).angle();
    double sw = ccw ? normAngle(ae - as) : -normAngle(as - ae);
    if(std::fabs(sw) < 1e-12) sw = ccw ? kTwoPi : -kTwoPi;
    return polar(c, r, as + 0.5 * sw);
}

std::vector<Vec2> ProfileLoop::polygon(double maxAngleStep) const {
    std::vector<Vec2> out;
    for(const auto &s : segs) {
        out.push_back(s.p0);
        if(!s.isArc) continue;
        const double as = (s.p0 - s.c).angle(), ae = (s.p1 - s.c).angle();
        double sw = s.ccw ? normAngle(ae - as) : -normAngle(as - ae);
        if(std::fabs(sw) < 1e-12) sw = s.ccw ? kTwoPi : -kTwoPi;
        const int n = std::max(2, int(std::ceil(std::fabs(sw) / maxAngleStep)));
        for(int i = 1; i < n; ++i) out.push_back(polar(s.c, s.r, as + sw * i / n));
    }
    return out;
}

bool pointInPolygon(const std::vector<Vec2> &poly, Vec2 p) {
    int winding = 0;
    const size_t n = poly.size();
    for(size_t i = 0; i < n; ++i) {
        const Vec2 a = poly[i], b = poly[(i + 1) % n];
        if(a.y <= p.y) {
            if(b.y > p.y && (b - a).cross(p - a) > 0) ++winding;
        } else if(b.y <= p.y && (b - a).cross(p - a) < 0) {
            --winding;
        }
    }
    return winding != 0;
}

bool Profile::contains(Vec2 p) const {
    if(!pointInPolygon(outer.polygon(), p)) return false;
    for(const auto &h : holes)
        if(pointInPolygon(h.polygon(), p)) return false;
    return true;
}

std::vector<Curve2> sketchCurves(const Sketch &sketch) {
    std::vector<Curve2> out;
    for(const auto &e : sketch.entities) {
        if(e.construction || !e.isCurve()) continue;
        Curve2 c;
        c.id = e.id;
        if(e.type == SkType::Line) {
            c.type = Curve2::Line;
            c.p0 = sketch.pointPos(e.a);
            c.p1 = sketch.pointPos(e.b);
        } else if(e.type == SkType::Circle) {
            c.type = Curve2::Circle;
            c.c = sketch.pointPos(e.a);
            c.r = e.r;
        } else {
            c.c = sketch.pointPos(e.a);
            const Vec2 s = sketch.pointPos(e.b), t = sketch.pointPos(e.c);
            c.r = distance(s, c.c);
            c.a0 = (s - c.c).angle();
            c.sweep = normAngle((t - c.c).angle() - c.a0);
            if(c.sweep < 1e-9) {
                c.type = Curve2::Circle; // start == end: a full circle
                c.a0 = 0.0;
                c.sweep = 0.0;
            } else {
                c.type = Curve2::Arc;
            }
        }
        out.push_back(c);
    }
    return out;
}

ProfileBuildResult buildProfiles(const std::vector<Curve2> &input) {
    ProfileBuildResult result;

    // Drop degenerate curves.
    std::vector<Curve2> curves;
    Bounds all;
    for(const auto &c : input) {
        if(c.type == Curve2::Line && distance(c.p0, c.p1) < 1e-9) continue;
        if(c.type != Curve2::Line && c.r < 1e-9) continue;
        if(c.type == Curve2::Arc && c.sweep < 1e-9) continue;
        curves.push_back(c);
        const Bounds b = curveBounds(c);
        all.add({b.minX, b.minY});
        all.add({b.maxX, b.maxY});
    }
    if(curves.empty()) return result;

    const double scale = std::max({1.0, all.maxX - all.minX, all.maxY - all.minY, std::fabs(all.minX),
                                   std::fabs(all.maxX), std::fabs(all.minY), std::fabs(all.maxY)});
    const double tol = 1e-7 * scale;

    // 1. Split parameters on every curve: endpoints plus intersections.
    std::vector<std::vector<Hit>> hits(curves.size());
    for(size_t i = 0; i < curves.size(); ++i) {
        const Curve2 &c = curves[i];
        if(c.type == Curve2::Line) {
            hits[i].push_back({0.0, c.p0});
            hits[i].push_back({1.0, c.p1});
        } else if(c.type == Curve2::Arc) {
            hits[i].push_back({0.0, polar(c.c, c.r, c.a0)});
            hits[i].push_back({c.sweep, polar(c.c, c.r, c.a0 + c.sweep)});
        }
    }
    std::vector<Bounds> bounds;
    for(const auto &c : curves) bounds.push_back(curveBounds(c));
    for(size_t i = 0; i < curves.size(); ++i)
        for(size_t j = i + 1; j < curves.size(); ++j)
            if(bounds[i].overlaps(bounds[j], tol)) intersect(curves[i], curves[j], tol, hits[i], hits[j]);

    // 2. Weld all split points into shared vertices.
    Welder welder(tol * 10.0);
    struct Split {
        double param;
        int v;
    };
    std::vector<std::vector<Split>> splits(curves.size());
    for(size_t i = 0; i < curves.size(); ++i)
        for(const Hit &h : hits[i]) splits[i].push_back({h.param, welder.add(h.p)});

    // 3. Cut curves into segments between consecutive split points.
    std::vector<GSeg> segs;
    for(size_t i = 0; i < curves.size(); ++i) {
        const Curve2 &c = curves[i];
        auto &sp = splits[i];
        std::sort(sp.begin(), sp.end(), [](const Split &a, const Split &b) { return a.param < b.param; });
        std::vector<Split> uniq;
        for(const Split &s : sp)
            if(uniq.empty() || uniq.back().v != s.v) uniq.push_back(s);
        if(c.type == Curve2::Circle) {
            while(uniq.size() > 1 && uniq.front().v == uniq.back().v) uniq.pop_back();
            // Give closed circles at least two vertices so every segment joins distinct vertices.
            if(uniq.empty()) {
                uniq.push_back({0.0, welder.add(polar(c.c, c.r, 0.0))});
                uniq.push_back({kPi, welder.add(polar(c.c, c.r, kPi))});
            } else if(uniq.size() == 1) {
                const double opp = normAngle(uniq[0].param + kPi);
                uniq.push_back({opp, welder.add(polar(c.c, c.r, opp))});
                std::sort(uniq.begin(), uniq.end(), [](const Split &a, const Split &b) { return a.param < b.param; });
            }
        }
        const size_t n = uniq.size();
        const size_t count = c.type == Curve2::Circle ? n : (n > 0 ? n - 1 : 0);
        for(size_t k = 0; k < count; ++k) {
            const Split &s0 = uniq[k];
            const Split &s1 = uniq[(k + 1) % n];
            if(s0.v == s1.v) continue;
            GSeg g;
            g.v0 = s0.v;
            g.v1 = s1.v;
            g.curveId = c.id;
            g.key = "c" + std::to_string(c.id) + "." + std::to_string(k);
            if(c.type == Curve2::Line) {
                g.isArc = false;
            } else {
                g.isArc = true;
                g.c = c.c;
                g.r = c.r;
                const double base = c.type == Curve2::Arc ? c.a0 : 0.0;
                g.aStart = base + s0.param;
                double sw = s1.param - s0.param;
                if(sw <= 0) sw += kTwoPi; // wrap for circles
                g.sweep = sw;
                if(g.sweep * g.r < tol) continue;
            }
            segs.push_back(g);
        }
    }
    const std::vector<Vec2> &pts = welder.points();

    // 4. Remove duplicate (overlapping) segments.
    {
        std::map<std::tuple<int, int, int, int64_t, int64_t>, size_t> seen;
        for(size_t i = 0; i < segs.size(); ++i) {
            GSeg &g = segs[i];
            int64_t mx = 0, my = 0;
            if(g.isArc) {
                const Vec2 m = polar(g.c, g.r, g.aStart + 0.5 * g.sweep);
                mx = int64_t(std::llround(m.x / (tol * 100)));
                my = int64_t(std::llround(m.y / (tol * 100)));
            }
            auto key = std::make_tuple(std::min(g.v0, g.v1), std::max(g.v0, g.v1), int(g.isArc), mx, my);
            if(seen.count(key)) g.alive = false;
            else seen[key] = i;
        }
    }

    // 5. Prune dangling segments repeatedly.
    std::vector<int> degree(pts.size(), 0);
    for(const auto &g : segs)
        if(g.alive) {
            ++degree[g.v0];
            ++degree[g.v1];
        }
    for(bool changed = true; changed;) {
        changed = false;
        for(auto &g : segs) {
            if(!g.alive) continue;
            if(degree[g.v0] < 2 || degree[g.v1] < 2) {
                g.alive = false;
                --degree[g.v0];
                --degree[g.v1];
                ++result.danglingSegments;
                changed = true;
            }
        }
    }

    // 6. Half-edges, sorted counter-clockwise around each vertex.
    std::vector<HalfEdge> hes;
    std::vector<std::vector<int>> outgoing(pts.size());
    auto quantize = [](double a) {
        a = normAngle(a);
        if(a > kTwoPi - 1e-9) a = 0.0;
        return int64_t(std::llround(a * 1e9));
    };
    for(size_t si = 0; si < segs.size(); ++si) {
        const GSeg &g = segs[si];
        if(!g.alive) continue;
        for(int dir = 0; dir < 2; ++dir) {
            HalfEdge h;
            h.seg = int(si);
            h.forward = dir == 0;
            h.from = h.forward ? g.v0 : g.v1;
            h.to = h.forward ? g.v1 : g.v0;
            if(!g.isArc) {
                const Vec2 d = pts[h.to] - pts[h.from];
                h.angleKey = quantize(d.angle());
                h.curvature = 0.0;
            } else if(h.forward) {
                h.angleKey = quantize(g.aStart + 0.5 * kPi);
                h.curvature = 1.0 / g.r;
            } else {
                h.angleKey = quantize(g.aStart + g.sweep - 0.5 * kPi);
                h.curvature = -1.0 / g.r;
            }
            outgoing[h.from].push_back(int(hes.size()));
            hes.push_back(h);
        }
    }
    std::vector<int> posInVertex(hes.size(), 0);
    for(auto &list : outgoing) {
        std::sort(list.begin(), list.end(), [&](int a, int b) {
            if(hes[a].angleKey != hes[b].angleKey) return hes[a].angleKey < hes[b].angleKey;
            return hes[a].curvature < hes[b].curvature;
        });
        for(size_t k = 0; k < list.size(); ++k) posInVertex[list[k]] = int(k);
    }
    auto twin = [](int h) { return h ^ 1; }; // half-edges were pushed in pairs
    auto nextHalfEdge = [&](int h) {
        const int t = twin(h);
        const auto &list = outgoing[hes[t].from];
        const int n = int(list.size());
        return list[(posInVertex[t] - 1 + n) % n];
    };

    // 7. Trace faces.
    struct Cycle {
        std::vector<int> hes;
        double area = 0.0;
        int component = -1;
    };
    std::vector<Cycle> cycles;
    std::vector<char> visited(hes.size(), 0);
    for(size_t start = 0; start < hes.size(); ++start) {
        if(visited[start]) continue;
        Cycle cyc;
        int h = int(start);
        size_t guard = 0;
        bool ok = true;
        do {
            if(visited[h] || ++guard > hes.size()) {
                ok = false;
                break;
            }
            visited[h] = 1;
            cyc.hes.push_back(h);
            cyc.area += segArea(segs[hes[h].seg], hes[h].forward, pts);
            h = nextHalfEdge(h);
        } while(h != int(start));
        if(ok && !cyc.hes.empty()) cycles.push_back(std::move(cyc));
    }

    // 8. Connected components (union-find over vertices).
    std::vector<int> parent(pts.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> findRoot = [&](int v) { return parent[v] == v ? v : parent[v] = findRoot(parent[v]); };
    for(const auto &g : segs)
        if(g.alive) parent[findRoot(g.v0)] = findRoot(g.v1);
    for(auto &c : cycles) c.component = findRoot(hes[c.hes[0]].from);

    auto cycleLoop = [&](const Cycle &c) {
        ProfileLoop loop;
        for(int h : c.hes) loop.segs.push_back(toProfileSeg(segs[hes[h].seg], hes[h].forward, pts));
        loop.area = c.area;
        return loop;
    };

    const double areaTol = tol * scale;
    std::vector<int> bounded;
    std::map<int, int> outerOfComponent; // component -> most negative cycle
    for(size_t i = 0; i < cycles.size(); ++i) {
        if(cycles[i].area > areaTol) {
            bounded.push_back(int(i));
        } else if(cycles[i].area < -areaTol) {
            auto it = outerOfComponent.find(cycles[i].component);
            if(it == outerOfComponent.end() || cycles[i].area < cycles[it->second].area)
                outerOfComponent[cycles[i].component] = int(i);
        }
    }

    // 9. Nest components: each component's outer boundary becomes a hole of the
    // smallest bounded face (of another component) that contains it.
    std::vector<std::vector<Vec2>> boundedPolys;
    for(int bi : bounded) boundedPolys.push_back(cycleLoop(cycles[bi]).polygon());
    std::map<int, std::vector<int>> holesOf; // bounded cycle index -> outer cycles
    for(const auto &[comp, oc] : outerOfComponent) {
        const Vec2 probe = pts[hes[cycles[oc].hes[0]].from];
        int best = -1;
        double bestArea = 1e300;
        for(size_t k = 0; k < bounded.size(); ++k) {
            const Cycle &f = cycles[bounded[k]];
            if(f.component == comp) continue;
            if(f.area < bestArea && pointInPolygon(boundedPolys[k], probe)) {
                best = bounded[k];
                bestArea = f.area;
            }
        }
        if(best >= 0) holesOf[best].push_back(oc);
    }

    // 10. Emit profiles.
    for(int bi : bounded) {
        Profile p;
        p.outer = cycleLoop(cycles[bi]);
        p.area = p.outer.area;
        for(int oc : holesOf[bi]) {
            ProfileLoop hole = cycleLoop(cycles[oc]);
            p.area += hole.area;
            p.holes.push_back(std::move(hole));
        }
        std::vector<std::string> keys;
        for(const auto &s : p.outer.segs) keys.push_back(s.key);
        std::sort(keys.begin(), keys.end());
        for(size_t k = 0; k < keys.size(); ++k) p.key += (k ? "," : "") + keys[k];
        p.sample = findInteriorPoint(p);
        result.profiles.push_back(std::move(p));
    }
    std::sort(result.profiles.begin(), result.profiles.end(),
              [](const Profile &a, const Profile &b) { return a.key < b.key; });
    return result;
}

} // namespace cad
