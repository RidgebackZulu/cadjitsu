#include "sketch/SketchOffset.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>

namespace cad {

namespace {

constexpr double kTiny = 1e-9;

// Points held together (shared ids, or coincident constraints between two
// points) resolve to one representative.
class PointJoins {
public:
    explicit PointJoins(const Sketch &s) {
        for(const auto &c : s.constraints) {
            if(c.type != SkCon::Coincident) continue;
            const SkEntity *a = s.find(c.e1), *b = s.find(c.e2);
            if(a && b && a->type == SkType::Point && b->type == SkType::Point) m_parent[root(c.e1)] = root(c.e2);
        }
    }
    int root(int p) const {
        auto it = m_parent.find(p);
        while(it != m_parent.end() && it->second != p) {
            p = it->second;
            it = m_parent.find(p);
        }
        return p;
    }

private:
    std::map<int, int> m_parent;
};

// The two ends of an open curve (in its own direction), or none for a circle.
bool endsOf(const SkEntity &e, int &from, int &to) {
    if(e.type == SkType::Line) {
        from = e.a;
        to = e.b;
        return true;
    }
    if(e.type == SkType::Arc) {
        from = e.b;
        to = e.c;
        return true;
    }
    return false;
}

bool isCurve(const SkEntity *e) {
    return e && (e->type == SkType::Line || e->type == SkType::Arc || e->type == SkType::Circle);
}

// The geometry of a chain link, in the chain's direction.
struct Link {
    const SkEntity *e = nullptr;
    bool reversed = false;
    Vec2 p0, p1, centre;
    double radius = 0.0;

    // The unit normal to the left of the running direction at `q` (on the curve).
    Vec2 leftNormal(Vec2 q) const {
        if(e->type == SkType::Line) return (p1 - p0).normalized().perp();
        // Running counter-clockwise, the left is towards the centre.
        const Vec2 out = (q - centre).normalized();
        return reversed ? out : -out;
    }
    // The offset radius for a distance to the left.
    double offsetRadius(double d) const { return e->type == SkType::Arc && reversed ? radius + d : radius - d; }
    // The sweep from p0 to p1 in the running direction (radians, in (0, 2 pi]).
    double sweep(Vec2 a, Vec2 b) const {
        const double s = reversed ? normAngle((a - centre).angle() - (b - centre).angle())
                                  : normAngle((b - centre).angle() - (a - centre).angle());
        return s < 1e-12 ? 2 * kPi : s;
    }
};

Link linkOf(const Sketch &s, const ChainLink &l) {
    Link k;
    k.e = s.find(l.id);
    k.reversed = l.reversed;
    const SkEntity &e = *k.e;
    if(e.type == SkType::Line) {
        k.p0 = s.pointPos(l.reversed ? e.b : e.a);
        k.p1 = s.pointPos(l.reversed ? e.a : e.b);
    } else if(e.type == SkType::Arc) {
        k.centre = s.pointPos(e.a);
        k.radius = s.arcRadius(e);
        k.p0 = s.pointPos(l.reversed ? e.c : e.b);
        k.p1 = s.pointPos(l.reversed ? e.b : e.c);
    } else {
        k.centre = s.pointPos(e.a);
        k.radius = e.r;
    }
    return k;
}

// Intersections of two offset curves taken whole (infinite lines, full circles).
std::vector<Vec2> intersect(const OffsetCurve &a, const OffsetCurve &b) {
    std::vector<Vec2> out;
    auto lineCircle = [&](Vec2 p, Vec2 d, Vec2 c, double r) {
        d = d.normalized();
        const Vec2 f = p - c;
        const double B = f.dot(d), C = f.length2() - r * r, disc = B * B - C;
        if(disc < -1e-9 * std::max(1.0, r * r)) return;
        const double sq = std::sqrt(std::max(0.0, disc));
        out.push_back(p + d * (-B - sq));
        out.push_back(p + d * (-B + sq));
    };
    const bool la = a.type == SkType::Line, lb = b.type == SkType::Line;
    if(la && lb) {
        const Vec2 d1 = a.end - a.start, d2 = b.end - b.start;
        const double den = d1.cross(d2);
        if(std::fabs(den) < 1e-12 * std::max(1.0, d1.length() * d2.length())) return out;
        out.push_back(a.start + d1 * ((b.start - a.start).cross(d2) / den));
    } else if(la || lb) {
        const OffsetCurve &l = la ? a : b, &c = la ? b : a;
        lineCircle(l.start, l.end - l.start, c.centre, c.radius);
    } else {
        const Vec2 dc = b.centre - a.centre;
        const double dist = dc.length();
        if(dist < kTiny) return out;
        const double x = (dist * dist + a.radius * a.radius - b.radius * b.radius) / (2 * dist);
        const double h2 = a.radius * a.radius - x * x;
        if(h2 < -1e-9 * std::max(1.0, a.radius * a.radius)) return out;
        const double hh = std::sqrt(std::max(0.0, h2));
        const Vec2 u = dc / dist, m = a.centre + u * x;
        out.push_back(m + u.perp() * hh);
        out.push_back(m - u.perp() * hh);
    }
    return out;
}

std::string describe(const SkEntity &e) {
    const char *kind = e.type == SkType::Line ? "line" : e.type == SkType::Arc ? "arc" : "circle";
    return std::string(kind) + " " + std::to_string(e.id);
}

// Points along a chain (for its enclosed area).
std::vector<Vec2> chainPolyline(const Sketch &s, const CurveChain &chain) {
    std::vector<Vec2> pts;
    for(const auto &l : chain.links) {
        const Link k = linkOf(s, l);
        if(k.e->type == SkType::Line) {
            pts.push_back(k.p0);
            continue;
        }
        const double a0 = k.e->type == SkType::Circle ? 0.0 : (k.p0 - k.centre).angle();
        const double sw = k.e->type == SkType::Circle ? 2 * kPi : k.sweep(k.p0, k.p1);
        const int n = 32;
        for(int i = 0; i < n; ++i) {
            const double t = a0 + (k.reversed ? -1.0 : 1.0) * sw * i / n;
            pts.push_back(k.centre + Vec2(std::cos(t), std::sin(t)) * k.radius);
        }
    }
    return pts;
}

} // namespace

int nearestCurve(const Sketch &s, Vec2 p, double *dist) {
    int best = 0;
    double bestD = std::numeric_limits<double>::infinity();
    for(const auto &e : s.entities) {
        if(!isCurve(&e)) continue;
        double d;
        if(e.type == SkType::Line) {
            const Vec2 a = s.pointPos(e.a), b = s.pointPos(e.b), ab = b - a;
            const double t = ab.length2() > 0 ? std::clamp((p - a).dot(ab) / ab.length2(), 0.0, 1.0) : 0.0;
            d = distance(p, a + ab * t);
        } else {
            const Vec2 c = s.pointPos(e.a);
            const double r = e.type == SkType::Circle ? e.r : s.arcRadius(e);
            d = std::fabs(distance(p, c) - r);
            if(e.type == SkType::Arc) {
                const Vec2 b = s.pointPos(e.b), cc = s.pointPos(e.c);
                const double sweep = normAngle((cc - c).angle() - (b - c).angle());
                if(normAngle((p - c).angle() - (b - c).angle()) > sweep) d = std::min(distance(p, b), distance(p, cc));
            }
        }
        if(d < bestD) {
            bestD = d;
            best = e.id;
        }
    }
    if(dist) *dist = bestD;
    return best;
}

std::vector<int> connectedCurves(const Sketch &s, int curveId) {
    const SkEntity *start = s.find(curveId);
    if(!isCurve(start)) return {};
    const PointJoins joins(s);
    std::map<int, std::vector<int>> atPoint; // joined point -> curves ending there
    for(const auto &e : s.entities) {
        int a = 0, b = 0;
        if(!isCurve(&e) || !endsOf(e, a, b)) continue;
        atPoint[joins.root(a)].push_back(e.id);
        if(joins.root(b) != joins.root(a)) atPoint[joins.root(b)].push_back(e.id);
    }
    std::vector<int> out{curveId};
    std::set<int> seen{curveId};
    for(size_t i = 0; i < out.size(); ++i) {
        const SkEntity &e = *s.find(out[i]);
        int a = 0, b = 0;
        if(!endsOf(e, a, b)) continue;
        for(int p : {joins.root(a), joins.root(b)}) {
            const auto &curves = atPoint[p];
            if(curves.size() != 2) continue; // an open end or a branch
            for(int c : curves)
                if(seen.insert(c).second) out.push_back(c);
        }
    }
    return out;
}

bool buildChains(const Sketch &s, const std::vector<int> &curveIds, std::vector<CurveChain> &chains, std::string &error) {
    chains.clear();
    const PointJoins joins(s);
    std::vector<int> open;
    std::set<int> picked;
    for(int id : curveIds) {
        const SkEntity *e = s.find(id);
        if(!isCurve(e) || !picked.insert(id).second) continue;
        if(e->type == SkType::Circle) chains.push_back({{{id, false}}, true});
        else open.push_back(id);
    }
    std::map<int, std::vector<int>> atPoint;
    for(int id : open) {
        int a = 0, b = 0;
        endsOf(*s.find(id), a, b);
        atPoint[joins.root(a)].push_back(id);
        atPoint[joins.root(b)].push_back(id);
    }
    for(const auto &[p, curves] : atPoint)
        if(curves.size() > 2) {
            error = "the picked curves branch at a point; offset each path on its own";
            return false;
        }
    auto ends = [&](int id, bool reversed) {
        int a = 0, b = 0;
        endsOf(*s.find(id), a, b);
        return reversed ? std::pair(joins.root(b), joins.root(a)) : std::pair(joins.root(a), joins.root(b));
    };
    std::set<int> used;
    // Open chains first (from an end that joins nothing), then loops.
    for(int pass = 0; pass < 2; ++pass) {
        for(int id : open) {
            if(used.count(id)) continue;
            int a = 0, b = 0;
            endsOf(*s.find(id), a, b);
            bool reversed = false;
            if(pass == 0) {
                if(atPoint[joins.root(a)].size() == 1) reversed = false;
                else if(atPoint[joins.root(b)].size() == 1) reversed = true;
                else continue;
            }
            CurveChain chain;
            chain.links.push_back({id, reversed});
            used.insert(id);
            int at = ends(id, reversed).second;
            for(;;) {
                int next = 0;
                for(int c : atPoint[at])
                    if(!used.count(c)) next = c;
                if(!next) break;
                const bool rev = ends(next, false).first != at;
                chain.links.push_back({next, rev});
                used.insert(next);
                at = ends(next, rev).second;
            }
            chain.closed = at == ends(chain.links.front().id, chain.links.front().reversed).first;
            chains.push_back(chain);
        }
    }
    if(chains.empty()) {
        error = "pick lines, arcs or circles to offset";
        return false;
    }
    // Closed chains run counter-clockwise, so "left" is inside for all of them.
    for(CurveChain &c : chains) {
        if(!c.closed || c.links.size() < 2 || outwardSign(s, c) < 0) continue;
        std::reverse(c.links.begin(), c.links.end());
        for(ChainLink &l : c.links) l.reversed = !l.reversed;
    }
    return true;
}

bool offsetGeometry(const Sketch &s, const std::vector<CurveChain> &chains, double d, std::vector<OffsetChain> &out,
                    std::string &error) {
    out.clear();
    for(const CurveChain &chain : chains) {
        std::vector<Link> links;
        for(const auto &l : chain.links) links.push_back(linkOf(s, l));
        OffsetChain oc;
        oc.closed = chain.closed;
        // Each curve moved on its own.
        for(const Link &k : links) {
            OffsetCurve c;
            c.source = k.e->id;
            c.construction = k.e->construction;
            c.reversed = k.reversed;
            if(k.e->type == SkType::Line) {
                c.type = SkType::Line;
                const Vec2 n = k.leftNormal(k.p0);
                c.start = k.p0 + n * d;
                c.end = k.p1 + n * d;
            } else {
                c.type = k.e->type;
                c.centre = k.centre;
                c.radius = k.offsetRadius(d);
                if(c.radius < 1e-6) {
                    error = "the offset is too big for " + describe(*k.e);
                    return false;
                }
                if(k.e->type == SkType::Arc) {
                    c.start = k.centre + (k.p0 - k.centre).normalized() * c.radius;
                    c.end = k.centre + (k.p1 - k.centre).normalized() * c.radius;
                }
            }
            oc.curves.push_back(c);
        }
        // Joined up again where they met.
        const size_t n = oc.curves.size();
        const size_t joints = chain.closed && links.front().e->type != SkType::Circle ? n : n - 1;
        for(size_t j = 0; j < joints; ++j) {
            OffsetCurve &a = oc.curves[j], &b = oc.curves[(j + 1) % n];
            Vec2 at = (a.end + b.start) * 0.5;
            if(distance(a.end, b.start) > 1e-9 * std::max(1.0, std::fabs(d))) {
                const auto hits = intersect(a, b);
                if(hits.empty()) {
                    error = "the offset curves of " + describe(*links[j].e) + " and " + describe(*links[(j + 1) % n].e) +
                            " do not meet";
                    return false;
                }
                at = *std::min_element(hits.begin(), hits.end(),
                                       [&](Vec2 p, Vec2 q) { return distance(p, at) < distance(q, at); });
            }
            a.end = at;
            b.start = at;
        }
        // Nothing turned inside out.
        for(size_t i = 0; i < n; ++i) {
            const Link &k = links[i];
            const OffsetCurve &c = oc.curves[i];
            bool ok = true;
            if(c.type == SkType::Line) {
                const Vec2 was = k.p1 - k.p0, now = c.end - c.start;
                ok = now.length() > 1e-6 && now.dot(was) > 0;
            } else if(c.type == SkType::Arc) {
                const double was = k.sweep(k.p0, k.p1), now = k.sweep(c.start, c.end);
                ok = distance(c.start, c.end) > 1e-6 && std::fabs(now - was) < kPi;
            }
            if(!ok) {
                error = "the offset is too big for " + describe(*k.e);
                return false;
            }
        }
        out.push_back(std::move(oc));
    }
    return true;
}

double outwardSign(const Sketch &s, const CurveChain &chain) {
    if(!chain.closed) return 1.0;
    const auto pts = chainPolyline(s, chain);
    double area = 0.0;
    for(size_t i = 0; i < pts.size(); ++i) area += pts[i].cross(pts[(i + 1) % pts.size()]);
    // Running counter-clockwise (positive area) the inside is on the left.
    return area > 0 ? -1.0 : 1.0;
}

double sideDistance(const Sketch &s, const std::vector<CurveChain> &chains, Vec2 p) {
    double best = std::numeric_limits<double>::infinity(), signedBest = 0.0;
    for(const auto &chain : chains)
        for(const auto &l : chain.links) {
            const Link k = linkOf(s, l);
            double dist, side;
            if(k.e->type == SkType::Line) {
                const Vec2 t = (k.p1 - k.p0).normalized();
                const double u = std::clamp((p - k.p0).dot(t), 0.0, (k.p1 - k.p0).length());
                dist = distance(p, k.p0 + t * u);
                side = t.cross(p - k.p0);
            } else {
                const double r = distance(p, k.centre);
                dist = std::fabs(r - k.radius);
                if(k.e->type == SkType::Arc) {
                    // Past the ends of the arc, the distance to its nearer end.
                    const double sw = k.sweep(k.p0, k.p1), at = k.sweep(k.p0, k.centre + (p - k.centre));
                    if(at > sw && at < 2 * kPi) dist = std::min(distance(p, k.p0), distance(p, k.p1));
                }
                side = (k.e->type == SkType::Arc && k.reversed) ? r - k.radius : k.radius - r;
            }
            if(dist < best) {
                best = dist;
                // The offset that passes through p: perpendicular for lines.
                if(k.e->type == SkType::Line) {
                    const Vec2 t = (k.p1 - k.p0).normalized();
                    signedBest = t.cross(p - k.p0);
                } else {
                    signedBest = side;
                }
            }
        }
    return signedBest;
}

std::vector<Vec2> offsetPolyline(const OffsetCurve &c) {
    if(c.type == SkType::Line) return {c.start, c.end};
    std::vector<Vec2> pts;
    double a0 = 0.0, sw = 2 * kPi;
    if(c.type == SkType::Arc) {
        a0 = (c.start - c.centre).angle();
        sw = c.reversed ? normAngle(a0 - (c.end - c.centre).angle()) : normAngle((c.end - c.centre).angle() - a0);
        if(sw < 1e-12) sw = 2 * kPi;
    }
    const int n = std::max(8, int(std::ceil(sw / (2 * kPi) * 64)));
    for(int i = 0; i <= n; ++i) {
        const double t = a0 + (c.reversed ? -1.0 : 1.0) * sw * i / n;
        pts.push_back(c.centre + Vec2(std::cos(t), std::sin(t)) * c.radius);
    }
    return pts;
}

bool applyOffset(Sketch &s, const std::vector<CurveChain> &chains, double d, const std::string &param,
                 const std::string &expr, const DimensionLookup &lookup, OffsetApplied &out, std::string &error) {
    out = OffsetApplied();
    std::vector<OffsetChain> geometry;
    if(!offsetGeometry(s, chains, d, geometry, error)) return false;

    struct Made {
        int id = 0, source = 0;
        SkType type = SkType::Line;
    };
    struct Candidate {
        SkCon type;
        int e1, e2;
        Vec2 label;
    };
    std::vector<Candidate> parallel, distances, tangents, radii;

    for(size_t ci = 0; ci < geometry.size(); ++ci) {
        const OffsetChain &g = geometry[ci];
        const CurveChain &chain = chains[ci];
        const size_t n = g.curves.size();
        std::vector<Made> made(n);
        if(g.curves.front().type == SkType::Circle) {
            const OffsetCurve &c = g.curves.front();
            const SkEntity *src = s.find(c.source);
            made[0] = {s.addCircle(src->a, c.radius, c.construction), c.source, SkType::Circle};
        } else {
            // One point per joint (and per open end), shared by the curves meeting there.
            std::vector<int> pts;
            for(size_t i = 0; i < n; ++i) pts.push_back(s.addPoint(g.curves[i].start.x, g.curves[i].start.y));
            if(!g.closed) pts.push_back(s.addPoint(g.curves.back().end.x, g.curves.back().end.y));
            for(size_t i = 0; i < n; ++i) {
                const OffsetCurve &c = g.curves[i];
                const int p0 = pts[i], p1 = pts[(i + 1) % pts.size()];
                if(c.type == SkType::Line) {
                    made[i] = {s.addLine(p0, p1, c.construction), c.source, SkType::Line};
                } else {
                    // Around the original's centre (a shared point: concentric).
                    const int centre = s.find(c.source)->a;
                    made[i] = {c.reversed ? s.addArc(centre, p1, p0, c.construction) : s.addArc(centre, p0, p1, c.construction),
                               c.source, SkType::Arc};
                }
            }
            for(int p : pts) out.entities.push_back(p);
        }
        for(size_t i = 0; i < n; ++i) {
            const Made &m = made[i];
            out.entities.push_back(m.id);
            const SkEntity &src = *s.find(m.source);
            if(m.type == SkType::Line) {
                parallel.push_back({SkCon::Parallel, m.id, m.source, {}});
                const Vec2 a = s.pointPos(src.a), b = s.pointPos(src.b);
                distances.push_back({SkCon::PointLineDistance, src.a, m.id, (b - a).normalized() * 4.0});
            } else {
                const double r = std::max(g.curves[i].radius, src.type == SkType::Circle ? src.r : s.arcRadius(src));
                radii.push_back({SkCon::OffsetRadius, m.id, m.source, Vec2(0.7071, 0.7071) * (r + 3.0)});
            }
        }
        // Tangent where the originals were (at joints with an arc).
        const size_t joints = g.closed && n > 1 ? n : n - 1;
        for(size_t j = 0; j < joints && n > 1; ++j) {
            const size_t k = (j + 1) % n;
            if(made[j].type == SkType::Line && made[k].type == SkType::Line) continue;
            const Link a = linkOf(s, chain.links[j]), b = linkOf(s, chain.links[k]);
            const Vec2 at = a.p1;
            const Vec2 ta = a.leftNormal(at).perp() * -1.0, tb = b.leftNormal(at).perp() * -1.0;
            if(std::fabs(ta.cross(tb)) < 1e-6 && ta.dot(tb) > 0) tangents.push_back({SkCon::Tangent, made[j].id, made[k].id, {}});
        }
    }

    // Constraints one at a time, leaving out any that would over-constrain.
    const double value = std::fabs(d);
    const DimensionLookup trialLookup = [&](const std::string &name, double &v) {
        if(name == param) {
            v = value;
            return true;
        }
        return lookup && lookup(name, v);
    };
    SolveOptions opts;
    opts.computeFreeEntities = false;
    auto solves = [&](const Sketch &sk, bool &redundant) {
        Sketch trial = sk;
        const SolveOutcome r = solveSketch(trial, trialLookup, opts);
        redundant = r.redundant;
        return r.ok && r.failed.empty();
    };
    bool wasRedundant = false;
    if(!solves(s, wasRedundant)) wasRedundant = true; // already in trouble: judge by solvability only
    std::vector<Candidate> all;
    for(auto *list : {&parallel, &distances, &tangents, &radii}) all.insert(all.end(), list->begin(), list->end());
    for(const Candidate &c : all) {
        Sketch trial = s;
        const int id = trial.addConstraint(c.type, c.e1, c.e2);
        SkConstraint &k = *trial.findConstraint(id);
        if(isDimension(c.type)) {
            k.label = c.label;
            if(out.dimension) {
                k.valueFrom = out.dimension;
            } else {
                k.param = param;
                k.expr = expr;
            }
        }
        bool redundant = false;
        if(!solves(trial, redundant) || (redundant && !wasRedundant)) continue;
        s = std::move(trial);
        if(isDimension(c.type) && !out.dimension) out.dimension = id;
    }
    return true;
}

} // namespace cad
