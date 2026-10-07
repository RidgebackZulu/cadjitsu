#include "sketch/SketchEdit.h"

#include "sketch/CurveIntersect.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cad {

namespace {

constexpr double kTwoPi = 2.0 * kPi;

// Where something crosses a curve: the parameter on it, the curve that
// crosses (0: the curve's own end), and the point.
struct Cut {
    double t = 0.0;
    int cutter = 0;
    Vec2 p;
};

double sketchTolerance(const Sketch &s) {
    double extent = 1.0;
    for(const auto &e : s.entities)
        if(e.type == SkType::Point) extent = std::max({extent, std::abs(e.x), std::abs(e.y)});
    return 1e-7 * extent;
}

bool usedByOtherCurve(const Sketch &s, int point, int except) {
    for(const auto &e : s.entities)
        if(e.id != except && e.type != SkType::Point && (e.a == point || e.b == point || e.c == point)) return true;
    return false;
}

void dropIfUnused(Sketch &s, int point) {
    if(point > 0 && !usedByOtherCurve(s, point, 0)) s.removeEntity(point);
}

// The crossings of curve `id` with every other curve, sorted by parameter.
std::vector<Cut> cutsOn(const Sketch &s, const Curve2 &curve, double tol) {
    std::vector<Cut> cuts;
    for(const auto &e : s.entities) {
        if(e.id == curve.id || !e.isCurve()) continue;
        const Curve2 other = curveOfEntity(s, e);
        std::vector<CurveHit> ha, hb;
        intersectCurves(curve, other, tol, ha, hb);
        for(const auto &h : ha) cuts.push_back({h.param, e.id, h.p});
    }
    std::sort(cuts.begin(), cuts.end(), [](const Cut &a, const Cut &b) { return a.t < b.t; });
    return cuts;
}

// The piece of `curveId` around `pick`: from `lo` to `hi` (cutter 0 means
// the curve's own end). Circles: the piece runs CCW from lo to hi.
struct Span {
    Curve2 curve;
    Cut lo, hi;
    bool whole = false;
};

bool trimSpan(const Sketch &s, int curveId, Vec2 pick, Span &span, std::string &error) {
    const SkEntity *e = s.find(curveId);
    if(!e || !e->isCurve()) {
        error = "pick a line, arc or circle to trim";
        return false;
    }
    const double tol = sketchTolerance(s);
    span.curve = curveOfEntity(s, *e);
    const Curve2 &c = span.curve;
    const double end = curveParamEnd(c);
    const double eps = c.type == Curve2::Line ? 1e-9 : 1e-9 / std::max(c.r, 1e-9);
    std::vector<Cut> cuts;
    for(const Cut &k : cutsOn(s, c, tol))
        if(c.type == Curve2::Circle || (k.t > eps && k.t < end - eps)) cuts.push_back(k);
    // Drop repeats (several curves crossing at one point).
    std::vector<Cut> unique;
    for(const Cut &k : cuts)
        if(unique.empty() || k.t - unique.back().t > eps) unique.push_back(k);
    if(c.type == Curve2::Circle && unique.size() > 1 && kTwoPi - unique.back().t + unique.front().t <= eps)
        unique.pop_back();

    double at = curveParam(c, pick);
    if(c.type == Curve2::Circle) {
        if(unique.size() < 2) {
            span.whole = true;
            return true;
        }
        at = normAngle(at);
        span.lo = unique.back();
        span.hi = unique.front();
        for(size_t i = 0; i + 1 < unique.size(); ++i) {
            if(at >= unique[i].t && at < unique[i + 1].t) {
                span.lo = unique[i];
                span.hi = unique[i + 1];
            }
        }
        return true;
    }
    at = std::clamp(at, 0.0, end);
    span.lo = {0.0, 0, curvePoint(c, 0.0)};
    span.hi = {end, 0, curvePoint(c, end)};
    for(const Cut &k : unique) {
        if(k.t <= at) span.lo = k;
        else if(k.t > at) {
            span.hi = k;
            break;
        }
    }
    span.whole = span.lo.cutter == 0 && span.hi.cutter == 0;
    return true;
}

// A point at cut `k`: the cutting curve's end point when the cut is there,
// otherwise a new point held on the cutting curve.
int pointAtCut(Sketch &s, const Cut &k, double tol, std::vector<int> &created) {
    if(const SkEntity *cutter = s.find(k.cutter)) {
        std::vector<int> ends;
        if(cutter->type == SkType::Line) ends = {cutter->a, cutter->b};
        else if(cutter->type == SkType::Arc) ends = {cutter->b, cutter->c};
        for(int p : ends)
            if(distance(s.pointPos(p), k.p) < 1e3 * tol) return p;
    }
    const int id = s.addPoint(k.p.x, k.p.y);
    created.push_back(id);
    if(k.cutter) s.addConstraint(SkCon::PointOnCurve, id, k.cutter);
    return id;
}

// Removes the dimensions of a line's length (it changes) and midpoints on it.
void dropLengthConstraints(Sketch &s, int curveId) {
    std::vector<int> drop;
    for(const auto &c : s.constraints) {
        if(c.type == SkCon::Distance && c.e1 == curveId && c.e2 == 0) drop.push_back(c.id);
        if(c.type == SkCon::Midpoint && c.e2 == curveId) drop.push_back(c.id);
    }
    for(int id : drop) s.removeConstraint(id);
}

std::vector<Vec2> sample(const Curve2 &c, double from, double to) {
    std::vector<Vec2> out;
    if(c.type == Curve2::Line) return {curvePoint(c, from), curvePoint(c, to)};
    if(to < from) to += kTwoPi;
    const int n = std::max(2, int(std::ceil((to - from) / 0.05)));
    for(int i = 0; i <= n; ++i) out.push_back(curvePoint(c, from + (to - from) * i / n));
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Trim

std::vector<Vec2> trimPreview(const Sketch &sketch, int curveId, Vec2 pick, bool &wholeCurve) {
    Span span;
    std::string why;
    wholeCurve = false;
    if(!trimSpan(sketch, curveId, pick, span, why)) return {};
    wholeCurve = span.whole;
    if(span.whole) {
        const Curve2 &c = span.curve;
        return sample(c, 0.0, c.type == Curve2::Circle ? kTwoPi - 1e-9 : curveParamEnd(c));
    }
    return sample(span.curve, span.lo.t, span.hi.t);
}

bool trimCurve(Sketch &sketch, int curveId, Vec2 pick, std::vector<int> &created, std::string &error) {
    created.clear();
    Span span;
    if(!trimSpan(sketch, curveId, pick, span, error)) return false;
    Sketch s = sketch;
    if(span.whole) {
        s.removeEntity(curveId);
        sketch = std::move(s);
        return true;
    }
    const double tol = sketchTolerance(s);
    const SkType type = s.find(curveId)->type;
    if(type == SkType::Circle) {
        // The rest of the circle runs CCW from the piece's end to its start.
        const int start = pointAtCut(s, span.hi, tol, created);
        const int end = pointAtCut(s, span.lo, tol, created);
        SkEntity &c = *s.find(curveId);
        c.type = SkType::Arc;
        c.b = start;
        c.c = end;
        c.r = 0.0;
        sketch = std::move(s);
        return true;
    }
    const bool atStart = span.lo.cutter == 0, atEnd = span.hi.cutter == 0;
    if(type == SkType::Line) dropLengthConstraints(s, curveId);
    if(atStart || atEnd) {
        // One end goes: the curve ends at the cut instead.
        const int p = pointAtCut(s, atStart ? span.hi : span.lo, tol, created);
        SkEntity &c = *s.find(curveId);
        int &slot = type == SkType::Line ? (atStart ? c.a : c.b) : (atStart ? c.b : c.c);
        const int old = slot;
        slot = p;
        dropIfUnused(s, old);
        sketch = std::move(s);
        return true;
    }
    // A piece from the middle: two curves.
    const int p1 = pointAtCut(s, span.lo, tol, created);
    const int p2 = pointAtCut(s, span.hi, tol, created);
    SkEntity &c = *s.find(curveId);
    const bool construction = c.construction;
    int piece = 0;
    if(type == SkType::Line) {
        const int oldEnd = c.b;
        c.b = p1;
        piece = s.addLine(p2, oldEnd, construction);
        // Orientation constraints hold for both pieces.
        const std::vector<SkConstraint> before = s.constraints;
        for(const auto &k : before) {
            const bool orient = k.type == SkCon::Horizontal || k.type == SkCon::Vertical ||
                                k.type == SkCon::Parallel || k.type == SkCon::Perpendicular;
            if(!orient || (k.e1 != curveId && k.e2 != curveId)) continue;
            if(k.e2 == 0 && (k.type == SkCon::Horizontal || k.type == SkCon::Vertical)) {
                s.addConstraint(k.type, piece);
            } else {
                s.addConstraint(k.type, k.e1 == curveId ? piece : k.e1, k.e2 == curveId ? piece : k.e2);
            }
        }
    } else {
        const int centre = c.a, oldEnd = c.c;
        c.c = p1;
        piece = s.addArc(centre, p2, oldEnd, construction);
        s.addConstraint(SkCon::Equal, curveId, piece);
    }
    created.push_back(piece);
    sketch = std::move(s);
    return true;
}

// ---------------------------------------------------------------------------
// Extend

namespace {

struct ExtendTarget {
    int point = 0;      // the end that moves
    Vec2 to;            // where it goes
    int cutter = 0;     // the curve it reaches
    std::vector<Vec2> path;
};

bool extendTarget(const Sketch &s, int curveId, Vec2 pick, ExtendTarget &out, std::string &error) {
    const SkEntity *e = s.find(curveId);
    if(!e || (e->type != SkType::Line && e->type != SkType::Arc)) {
        error = "pick a line or an arc to extend";
        return false;
    }
    const double tol = sketchTolerance(s);
    const int first = e->type == SkType::Line ? e->a : e->b;
    const int second = e->type == SkType::Line ? e->b : e->c;
    const bool fromSecond = distance(s.pointPos(second), pick) <= distance(s.pointPos(first), pick);
    out.point = fromSecond ? second : first;
    if(usedByOtherCurve(s, out.point, curveId)) {
        error = "that end is joined to another curve";
        return false;
    }
    const Vec2 endPos = s.pointPos(out.point);
    double best = std::numeric_limits<double>::max();
    if(e->type == SkType::Line) {
        const Vec2 other = s.pointPos(fromSecond ? first : second);
        const Vec2 dir = (endPos - other).normalized();
        double reach = 1e4;
        for(const auto &p : s.entities)
            if(p.type == SkType::Point) reach = std::max(reach, 4.0 * (std::abs(p.x) + std::abs(p.y)));
        Curve2 ray;
        ray.type = Curve2::Line;
        ray.p0 = endPos;
        ray.p1 = endPos + dir * reach;
        for(const auto &k : s.entities) {
            if(k.id == curveId || !k.isCurve()) continue;
            std::vector<CurveHit> ha, hb;
            intersectCurves(ray, curveOfEntity(s, k), tol, ha, hb);
            for(const auto &h : ha) {
                const double d = h.param * reach;
                if(d > 1e3 * tol && d < best) {
                    best = d;
                    out.to = h.p;
                    out.cutter = k.id;
                }
            }
        }
        if(out.cutter) out.path = {endPos, out.to};
    } else {
        const Vec2 centre = s.pointPos(e->a);
        Curve2 circle;
        circle.type = Curve2::Circle;
        circle.c = centre;
        circle.r = distance(s.pointPos(e->b), centre);
        const double endAngle = (endPos - centre).angle();
        const double sweep = normAngle((s.pointPos(e->c) - centre).angle() - (s.pointPos(e->b) - centre).angle());
        for(const auto &k : s.entities) {
            if(k.id == curveId || !k.isCurve()) continue;
            std::vector<CurveHit> ha, hb;
            intersectCurves(circle, curveOfEntity(s, k), tol, ha, hb);
            for(const auto &h : ha) {
                // Beyond the end: CCW past the arc's end, CW before its start.
                const double d = fromSecond ? normAngle(h.param - endAngle) : normAngle(endAngle - h.param);
                if(d > 1e-7 && d < kTwoPi - sweep - 1e-7 && d < best) {
                    best = d;
                    out.to = h.p;
                    out.cutter = k.id;
                }
            }
        }
        if(out.cutter) {
            const int n = std::max(2, int(std::ceil(best / 0.05)));
            for(int i = 0; i <= n; ++i) {
                const double a = endAngle + (fromSecond ? 1.0 : -1.0) * best * i / n;
                out.path.push_back(centre + Vec2(std::cos(a), std::sin(a)) * circle.r);
            }
        }
    }
    if(!out.cutter) {
        error = "nothing to extend to in that direction";
        return false;
    }
    return true;
}

} // namespace

std::vector<Vec2> extendPreview(const Sketch &sketch, int curveId, Vec2 pick) {
    ExtendTarget t;
    std::string why;
    return extendTarget(sketch, curveId, pick, t, why) ? t.path : std::vector<Vec2>{};
}

bool extendCurve(Sketch &sketch, int curveId, Vec2 pick, std::string &error) {
    ExtendTarget t;
    if(!extendTarget(sketch, curveId, pick, t, error)) return false;
    Sketch s = sketch;
    if(s.find(curveId)->type == SkType::Line) dropLengthConstraints(s, curveId);
    SkEntity &p = *s.find(t.point);
    p.x = t.to.x;
    p.y = t.to.y;
    // Held where it landed: on the end point of the curve reached, or on it.
    const double tol = sketchTolerance(s);
    const SkEntity &cutter = *s.find(t.cutter);
    int shared = 0;
    if(cutter.type == SkType::Line || cutter.type == SkType::Arc) {
        for(int q : {cutter.type == SkType::Line ? cutter.a : cutter.b, cutter.type == SkType::Line ? cutter.b : cutter.c})
            if(distance(s.pointPos(q), t.to) < 1e3 * tol) shared = q;
    }
    if(shared) s.addConstraint(SkCon::Coincident, t.point, shared);
    else s.addConstraint(SkCon::PointOnCurve, t.point, t.cutter);
    sketch = std::move(s);
    return true;
}

// ---------------------------------------------------------------------------
// Corner fillet

namespace {

struct Corner {
    int line1 = 0, line2 = 0;  // the two lines meeting at the corner
    int far1 = 0, far2 = 0;    // their other ends
};

bool cornerLines(const Sketch &s, int corner, Corner &out) {
    int lines = 0, others = 0;
    for(const auto &e : s.entities) {
        if(e.type == SkType::Point || e.isText()) continue;
        const bool uses = e.a == corner || e.b == corner || e.c == corner;
        if(!uses) continue;
        if(e.type == SkType::Line) {
            ++lines;
            (out.line1 ? out.line2 : out.line1) = e.id;
            (out.far1 ? out.far2 : out.far1) = e.a == corner ? e.b : e.a;
        } else {
            ++others;
        }
    }
    return lines == 2 && others == 0;
}

struct FilletShape {
    Vec2 centre, t1, t2;
};

bool filletShape(const Sketch &s, int corner, const Corner &k, double radius, FilletShape &out, std::string &error) {
    if(!(radius > 0)) {
        error = "the fillet radius must be more than 0";
        return false;
    }
    const Vec2 q = s.pointPos(corner);
    const Vec2 f1 = s.pointPos(k.far1), f2 = s.pointPos(k.far2);
    const double len1 = distance(q, f1), len2 = distance(q, f2);
    if(len1 < 1e-9 || len2 < 1e-9) {
        error = "a line at the corner has no length";
        return false;
    }
    const Vec2 u1 = (f1 - q) / len1, u2 = (f2 - q) / len2;
    const double theta = std::acos(std::clamp(u1.dot(u2), -1.0, 1.0));
    if(theta < 1e-4 || theta > kPi - 1e-4) {
        error = "the lines are in line: there is no corner to round";
        return false;
    }
    const double back = radius / std::tan(theta / 2.0);
    if(back >= len1 - 1e-9 || back >= len2 - 1e-9) {
        error = "the radius is too big for the lines at this corner";
        return false;
    }
    out.t1 = q + u1 * back;
    out.t2 = q + u2 * back;
    out.centre = q + (u1 + u2).normalized() * (radius / std::sin(theta / 2.0));
    return true;
}

} // namespace

int filletCornerNear(const Sketch &sketch, Vec2 p, double maxDistance) {
    int best = 0;
    double bestDist = maxDistance;
    for(const auto &e : sketch.entities) {
        if(e.type != SkType::Point) continue;
        const double d = distance({e.x, e.y}, p);
        if(d > bestDist) continue;
        Corner k;
        if(!cornerLines(sketch, e.id, k)) continue;
        best = e.id;
        bestDist = d;
    }
    return best;
}

std::vector<Vec2> filletPreview(const Sketch &sketch, int corner, double radius) {
    Corner k;
    FilletShape f;
    std::string why;
    if(!cornerLines(sketch, corner, k) || !filletShape(sketch, corner, k, radius, f, why)) return {};
    return {f.centre, f.t1, f.t2};
}

bool filletCorner(Sketch &sketch, int corner, double radius, int &arcId, std::vector<int> &created,
                  std::string &error) {
    created.clear();
    Corner k;
    if(!cornerLines(sketch, corner, k)) {
        error = "pick a corner where two lines meet";
        return false;
    }
    FilletShape f;
    if(!filletShape(sketch, corner, k, radius, f, error)) return false;
    Sketch s = sketch;
    const bool construction = s.find(k.line1)->construction && s.find(k.line2)->construction;
    const int p1 = s.addPoint(f.t1.x, f.t1.y, construction);
    const int p2 = s.addPoint(f.t2.x, f.t2.y, construction);
    const int pc = s.addPoint(f.centre.x, f.centre.y, construction);
    for(auto [line, p] : {std::pair{k.line1, p1}, std::pair{k.line2, p2}}) {
        SkEntity &l = *s.find(line);
        (l.a == corner ? l.a : l.b) = p;
    }
    // The corner stays as a virtual sharp: on both lines, and what measured
    // a line's length now measures from it.
    s.find(corner)->construction = true;
    s.addConstraint(SkCon::PointOnCurve, corner, k.line1);
    s.addConstraint(SkCon::PointOnCurve, corner, k.line2);
    for(auto &c : s.constraints) {
        if(c.type != SkCon::Distance || c.e2 != 0) continue;
        if(c.e1 != k.line1 && c.e1 != k.line2) continue;
        c.e2 = c.e1 == k.line1 ? k.far1 : k.far2;
        c.e1 = corner;
    }
    // Midpoints of the lines no longer mean what they did.
    std::vector<int> drop;
    for(const auto &c : s.constraints)
        if(c.type == SkCon::Midpoint && (c.e2 == k.line1 || c.e2 == k.line2)) drop.push_back(c.id);
    for(int id : drop) s.removeConstraint(id);
    // The arc runs CCW, the short way round.
    const bool ccw = (f.t1 - f.centre).cross(f.t2 - f.centre) > 0;
    arcId = ccw ? s.addArc(pc, p1, p2, construction) : s.addArc(pc, p2, p1, construction);
    s.addConstraint(SkCon::Tangent, k.line1, arcId);
    s.addConstraint(SkCon::Tangent, k.line2, arcId);
    created = {p1, p2, pc, arcId};
    sketch = std::move(s);
    return true;
}

// ---------------------------------------------------------------------------
// Slot and polygon

bool addSlot(Sketch &sketch, int c1, int c2, double width, SlotIds &out, std::string &error) {
    const SkEntity *e1 = sketch.find(c1), *e2 = sketch.find(c2);
    if(!e1 || !e2 || e1->type != SkType::Point || e2->type != SkType::Point) {
        error = "a slot needs two centre points";
        return false;
    }
    const Vec2 a = sketch.pointPos(c1), b = sketch.pointPos(c2);
    if(distance(a, b) < 1e-9) {
        error = "the slot's centres are in the same place";
        return false;
    }
    if(!(width > 0)) {
        error = "the slot's width must be more than 0";
        return false;
    }
    Sketch s = sketch;
    const Vec2 u = (b - a).normalized(), n = u.perp();
    const double r = width / 2.0;
    const Vec2 a1 = a + n * r, a2 = b + n * r, b2 = b - n * r, b1 = a - n * r;
    const int pa1 = s.addPoint(a1.x, a1.y), pa2 = s.addPoint(a2.x, a2.y);
    const int pb2 = s.addPoint(b2.x, b2.y), pb1 = s.addPoint(b1.x, b1.y);
    out.centreLine = s.addLine(c1, c2, true);
    out.line1 = s.addLine(pa1, pa2);
    out.line2 = s.addLine(pb2, pb1);
    out.arc2 = s.addArc(c2, pb2, pa2); // round the c2 end, CCW from -n to +n
    out.arc1 = s.addArc(c1, pa1, pb1); // round the c1 end, CCW from +n to -n
    for(int line : {out.line1, out.line2})
        for(int arc : {out.arc1, out.arc2}) s.addConstraint(SkCon::Tangent, line, arc);
    s.addConstraint(SkCon::Equal, out.arc1, out.arc2);
    sketch = std::move(s);
    return true;
}

std::vector<Vec2> regularPolygonCorners(Vec2 centre, Vec2 vertex, int sides, bool inscribed) {
    std::vector<Vec2> out;
    if(sides < 3) return out;
    const double d = distance(centre, vertex);
    const double a0 = (vertex - centre).angle();
    const double step = kTwoPi / sides;
    const double radius = inscribed ? d : d / std::cos(kPi / sides);
    const double start = inscribed ? a0 : a0 - step / 2.0;
    for(int i = 0; i < sides; ++i)
        out.push_back(centre + Vec2(std::cos(start + step * i), std::sin(start + step * i)) * radius);
    return out;
}

bool addRegularPolygon(Sketch &sketch, int centre, Vec2 vertex, int sides, bool inscribed, PolygonIds &out,
                       std::string &error) {
    const SkEntity *c = sketch.find(centre);
    if(!c || c->type != SkType::Point) {
        error = "a polygon needs a centre point";
        return false;
    }
    if(sides < 3 || sides > 64) {
        error = "a polygon has 3 to 64 sides";
        return false;
    }
    const Vec2 o = sketch.pointPos(centre);
    if(distance(o, vertex) < 1e-9) {
        error = "the polygon has no size";
        return false;
    }
    Sketch s = sketch;
    const std::vector<Vec2> corners = regularPolygonCorners(o, vertex, sides, inscribed);
    out.circle = s.addCircle(centre, distance(o, vertex), true);
    std::vector<int> points;
    for(const Vec2 &p : corners) points.push_back(s.addPoint(p.x, p.y));
    out.lines.clear();
    for(int i = 0; i < sides; ++i) out.lines.push_back(s.addLine(points[i], points[(i + 1) % sides]));
    for(int i = 0; i < sides; ++i) {
        if(inscribed) s.addConstraint(SkCon::PointOnCurve, points[i], out.circle);
        else s.addConstraint(SkCon::Tangent, out.lines[i], out.circle);
        if(i > 0) s.addConstraint(SkCon::Equal, out.lines[0], out.lines[i]);
    }
    sketch = std::move(s);
    return true;
}

} // namespace cad
