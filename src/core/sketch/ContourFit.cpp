#include "sketch/ContourFit.h"

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

double pointLineDistance(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const double l = d.length();
    if(l < 1e-12) return distance(p, a);
    return std::fabs((p - a).cross(d)) / l;
}

// The dense points from index i to j (inclusive) of a closed loop.
std::vector<Vec2> span(const std::vector<Vec2> &loop, size_t i, size_t j) {
    std::vector<Vec2> out;
    const size_t n = loop.size();
    for(size_t k = i;; k = (k + 1) % n) {
        out.push_back(loop[k]);
        if(k == j) break;
    }
    return out;
}

// Douglas-Peucker on the open run i..j: the indices of the kept vertices
// strictly between i and j.
void simplify(const std::vector<Vec2> &loop, size_t i, size_t j, double tol, std::vector<size_t> &out) {
    const size_t n = loop.size();
    const size_t len = (j + n - i) % n;
    if(len < 2) return;
    double worst = -1;
    size_t at = i;
    for(size_t k = (i + 1) % n; k != j; k = (k + 1) % n) {
        const double d = pointLineDistance(loop[k], loop[i], loop[j]);
        if(d > worst) {
            worst = d;
            at = k;
        }
    }
    if(worst <= tol) return;
    simplify(loop, i, at, tol, out);
    out.push_back(at);
    simplify(loop, at, j, tol, out);
}

double maxCircleDeviation(const std::vector<Vec2> &pts, Vec2 c, double r) {
    double worst = 0;
    for(const Vec2 &p : pts) worst = std::max(worst, std::fabs(distance(p, c) - r));
    return worst;
}

// The angle an arc through `pts` (in order) sweeps about `c`, signed.
double sweepOf(const std::vector<Vec2> &pts, Vec2 c) {
    double total = 0;
    for(size_t k = 1; k < pts.size(); ++k) {
        double d = (pts[k] - c).angle() - (pts[k - 1] - c).angle();
        while(d > kPi) d -= 2 * kPi;
        while(d < -kPi) d += 2 * kPi;
        total += d;
    }
    return total;
}

} // namespace

bool fitCircle(const std::vector<Vec2> &pts, Vec2 &centre, double &radius) {
    if(pts.size() < 3) return false;
    // Kasa fit about the mean (better conditioned).
    Vec2 m;
    for(const Vec2 &p : pts) m = m + p;
    m = m / double(pts.size());
    double suu = 0, suv = 0, svv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for(const Vec2 &p : pts) {
        const double u = p.x - m.x, v = p.y - m.y;
        suu += u * u;
        suv += u * v;
        svv += v * v;
        suuu += u * u * u;
        svvv += v * v * v;
        suvv += u * v * v;
        svuu += v * u * u;
    }
    const double det = suu * svv - suv * suv;
    if(std::fabs(det) < 1e-12 * std::max(1.0, suu * svv)) return false;
    const double b1 = 0.5 * (suuu + suvv), b2 = 0.5 * (svvv + svuu);
    const double uc = (b1 * svv - b2 * suv) / det, vc = (suu * b2 - suv * b1) / det;
    centre = m + Vec2(uc, vc);
    radius = std::sqrt(uc * uc + vc * vc + (suu + svv) / double(pts.size()));
    return std::isfinite(radius);
}

FitLoop fitContour(const std::vector<Vec2> &input, double tol) {
    FitLoop out;
    if(input.size() < 3) return out;
    // Evenly spaced points (a straight run of the outline may be given by its
    // ends only, which would let a circle pass for a line).
    std::vector<Vec2> loop;
    const double step = std::max(0.5, tol * 0.5);
    for(size_t i = 0; i < input.size(); ++i) {
        const Vec2 a = input[i], b = input[(i + 1) % input.size()];
        const int k = std::max(1, int(std::ceil(distance(a, b) / step)));
        for(int q = 0; q < k; ++q) loop.push_back(a + (b - a) * (double(q) / k));
    }
    const size_t n = loop.size();
    // A whole circle?
    if(n >= 12) {
        Vec2 c;
        double r;
        if(fitCircle(loop, c, r) && maxCircleDeviation(loop, c, r) <= tol && r > 3 * tol) {
            out.circle = true;
            out.centre = c;
            out.radius = r;
            return out;
        }
    }
    // Corners: Douglas-Peucker from the point furthest from the middle (a
    // corner, if there are any) round to the point furthest from it, and back.
    Vec2 mid;
    for(const Vec2 &p : loop) mid = mid + p;
    mid = mid / double(n);
    size_t s0 = 0;
    for(size_t k = 1; k < n; ++k)
        if(distance(loop[k], mid) > distance(loop[s0], mid)) s0 = k;
    size_t s1 = s0;
    for(size_t k = 0; k < n; ++k)
        if(distance(loop[k], loop[s0]) > distance(loop[s1], loop[s0])) s1 = k;
    std::vector<size_t> verts{s0};
    simplify(loop, s0, s1, tol, verts);
    verts.push_back(s1);
    simplify(loop, s1, s0, tol, verts);
    const size_t m = verts.size();
    if(m < 3) return out;
    // Start at the longest side (most likely a straight one), so no run of
    // arc pieces is split across the start of the loop.
    {
        size_t longest = 0;
        for(size_t q = 1; q < m; ++q)
            if(distance(loop[verts[q]], loop[verts[(q + 1) % m]]) >
               distance(loop[verts[longest]], loop[verts[(longest + 1) % m]]))
                longest = q;
        std::rotate(verts.begin(), verts.begin() + std::ptrdiff_t(longest), verts.end());
    }

    // Runs of short segments turning the same way that fit a circle become
    // arcs; the rest stay lines.
    auto turn = [&](size_t k) { // at vertex k (between segments k-1 and k)
        const Vec2 a = loop[verts[(k + m - 1) % m]], b = loop[verts[k]], c = loop[verts[(k + 1) % m]];
        return (b - a).cross(c - b);
    };
    std::vector<std::vector<Vec2>> pieces; // each line's outline points (none for arcs)
    size_t k = 0;
    while(k < m) {
        size_t best = k; // last segment of an accepted arc starting at segment k
        Vec2 bestC;
        double bestR = 0;
        for(size_t j = k + 1; j < m; ++j) { // at least two segments
            bool sameWay = true;
            const double t0 = turn(k + 1);
            for(size_t q = k + 1; q <= j; ++q)
                if(turn(q) * t0 <= 0) sameWay = false;
            if(!sameWay) break;
            const std::vector<Vec2> pts = span(loop, verts[k], verts[(j + 1) % m]);
            Vec2 c;
            double r;
            if(!fitCircle(pts, c, r) || maxCircleDeviation(pts, c, r) > tol) break;
            if(std::fabs(sweepOf(pts, c)) < 20.0 * kPi / 180.0 || r > 1e4 * tol) continue;
            // Each piece a short chord of it (a long one is a straight side).
            bool shortChords = true;
            for(size_t q = k; q <= j; ++q)
                if(distance(loop[verts[q]], loop[verts[(q + 1) % m]]) > 1.2 * r) shortChords = false;
            if(!shortChords) break;
            best = j;
            bestC = c;
            bestR = r;
        }
        FitSegment s;
        if(best > k) {
            const std::vector<Vec2> pts = span(loop, verts[k], verts[(best + 1) % m]);
            s.arc = true;
            s.centre = bestC;
            s.radius = bestR;
            // Ends and middle on the circle.
            auto onCircle = [&](Vec2 p) { return bestC + (p - bestC).normalized() * bestR; };
            s.a = onCircle(pts.front());
            s.b = onCircle(pts.back());
            s.mid = onCircle(pts[pts.size() / 2]);
            out.segments.push_back(s);
            pieces.push_back({});
            k = best + 1;
        } else {
            s.a = loop[verts[k]];
            s.b = loop[verts[(k + 1) % m]];
            out.segments.push_back(s);
            pieces.push_back(span(loop, verts[k], verts[(k + 1) % m]));
            ++k;
        }
    }
    // Lines in a row that are one straight side become one: two at a slight
    // kink, or two with a jog between them (a resampled photo's pixel steps),
    // when all their points lie on one line.
    auto tryMerge = [&](size_t q, size_t count) {
        const size_t ns = out.segments.size();
        if(ns - count + 1 < 3) return false;
        std::vector<size_t> idx;
        for(size_t i = 0; i < count; ++i) idx.push_back((q + i) % ns);
        for(size_t i : idx)
            if(out.segments[i].arc) return false;
        const FitSegment &first = out.segments[idx.front()], &last = out.segments[idx.back()];
        const Vec2 da = (first.b - first.a).normalized(), db = (last.b - last.a).normalized();
        if(da.dot(db) < std::cos(4.0 * kPi / 180.0)) return false;
        std::vector<Vec2> pts;
        for(size_t i : idx) pts.insert(pts.end(), pieces[i].begin(), pieces[i].end());
        // Straight within tolerance about the best line through them (the
        // corners at either end are blurred: left out).
        std::vector<Vec2> inner;
        for(const Vec2 &p : pts)
            if(distance(p, first.a) > 2 * tol && distance(p, last.b) > 2 * tol) inner.push_back(p);
        if(inner.size() < 3) return false;
        Vec2 c;
        for(const Vec2 &p : inner) c = c + p;
        c = c / double(inner.size());
        double sxx = 0, sxy = 0, syy = 0;
        for(const Vec2 &p : inner) {
            const Vec2 r = p - c;
            sxx += r.x * r.x;
            sxy += r.x * r.y;
            syy += r.y * r.y;
        }
        const double ang = 0.5 * std::atan2(2 * sxy, sxx - syy);
        const Vec2 dir(std::cos(ang), std::sin(ang));
        for(const Vec2 &p : inner)
            if(std::fabs((p - c).cross(dir)) > 1.5 * tol) return false;
        const Vec2 end = last.b;
        out.segments[idx.front()].b = end;
        pieces[idx.front()] = std::move(pts);
        // Erase the others, highest index first.
        std::vector<size_t> rest(idx.begin() + 1, idx.end());
        std::sort(rest.rbegin(), rest.rend());
        for(size_t i : rest) {
            out.segments.erase(out.segments.begin() + std::ptrdiff_t(i));
            pieces.erase(pieces.begin() + std::ptrdiff_t(i));
        }
        return true;
    };
    for(bool again = true; again;) {
        again = false;
        for(size_t q = 0; q < out.segments.size() && !again; ++q) again = tryMerge(q, 2) || tryMerge(q, 3);
    }
    // Straight sides, refitted to their outline points away from the ends
    // (a photo blurs corners): a point on the side and its direction.
    struct Side {
        Vec2 p, d;
    };
    std::vector<Side> sides(out.segments.size());
    for(size_t q = 0; q < out.segments.size(); ++q) {
        const FitSegment &seg = out.segments[q];
        if(seg.arc) continue;
        std::vector<Vec2> inner;
        for(const Vec2 &p : pieces[q])
            if(distance(p, seg.a) > 2 * tol && distance(p, seg.b) > 2 * tol) inner.push_back(p);
        sides[q] = {(seg.a + seg.b) * 0.5, (seg.b - seg.a).normalized()};
        if(inner.size() < 3) continue;
        Vec2 c;
        for(const Vec2 &p : inner) c = c + p;
        c = c / double(inner.size());
        double sxx = 0, sxy = 0, syy = 0;
        for(const Vec2 &p : inner) {
            const Vec2 r = p - c;
            sxx += r.x * r.x;
            sxy += r.x * r.y;
            syy += r.y * r.y;
        }
        const double ang = 0.5 * std::atan2(2 * sxy, sxx - syy);
        Vec2 d(std::cos(ang), std::sin(ang));
        if(d.dot(seg.b - seg.a) < 0) d = d * -1.0;
        sides[q] = {c, d};
    }
    auto meet = [&](size_t i, size_t j, Vec2 &x) { // where sides i and j cross
        const double den = sides[i].d.cross(sides[j].d);
        if(std::fabs(den) < 1e-6) return false;
        x = sides[i].p + sides[i].d * ((sides[j].p - sides[i].p).cross(sides[j].d) / den);
        return true;
    };
    // A sliver line between two lines is a blurred corner: they meet there.
    for(size_t q = 0; q < out.segments.size() && out.segments.size() > 3;) {
        const size_t n2 = out.segments.size(), pq = (q + n2 - 1) % n2, nq = (q + 1) % n2;
        const FitSegment &cur = out.segments[q];
        Vec2 x;
        if(!cur.arc && !out.segments[pq].arc && !out.segments[nq].arc && distance(cur.a, cur.b) < 3 * tol &&
           meet(pq, nq, x) && distance(x, (cur.a + cur.b) * 0.5) < 3 * tol) {
            out.segments.erase(out.segments.begin() + std::ptrdiff_t(q));
            pieces.erase(pieces.begin() + std::ptrdiff_t(q));
            sides.erase(sides.begin() + std::ptrdiff_t(q));
        } else {
            ++q;
        }
    }
    // Corners between two sides where the refitted sides cross; a side's end
    // at an arc onto the refitted side.
    const size_t ns = out.segments.size();
    for(size_t q = 0; q < ns; ++q) {
        const size_t nq = (q + 1) % ns;
        FitSegment &cur = out.segments[q], &nxt = out.segments[nq];
        Vec2 x;
        if(!cur.arc && !nxt.arc && meet(q, nq, x) && distance(x, (cur.b + nxt.a) * 0.5) < 3 * tol) {
            cur.b = x;
            nxt.a = x;
            continue;
        }
        auto onSide = [&](size_t i, Vec2 p) { return sides[i].p + sides[i].d * (p - sides[i].p).dot(sides[i].d); };
        if(!cur.arc && nxt.arc) cur.b = onSide(q, cur.b);
        if(cur.arc && !nxt.arc) nxt.a = onSide(nq, nxt.a);
    }
    // Close the joins: each segment starts where the one before ends.
    for(size_t q = 0; q < out.segments.size(); ++q) {
        FitSegment &cur = out.segments[q];
        FitSegment &nxt = out.segments[(q + 1) % out.segments.size()];
        const Vec2 joint = (cur.b + nxt.a) * 0.5;
        cur.b = joint;
        nxt.a = joint;
    }
    return out;
}

std::vector<int> addFittedLoop(Sketch &s, const FitLoop &loop, const std::function<Vec2(Vec2)> &map,
                               std::vector<SuggestedConstraint> &suggested, double levelDegrees, double tangentDegrees) {
    std::vector<int> curves;
    if(loop.circle) {
        const Vec2 c = map(loop.centre), e = map(loop.centre + Vec2(loop.radius, 0));
        curves.push_back(s.addCircle(c, distance(c, e)));
        return curves;
    }
    const size_t n = loop.segments.size();
    if(n < 2) return curves;
    std::vector<int> joints;
    for(const FitSegment &seg : loop.segments) {
        const Vec2 p = map(seg.a);
        joints.push_back(s.addPoint(p.x, p.y));
    }
    const double level = levelDegrees * kPi / 180.0;
    for(size_t q = 0; q < n; ++q) {
        const FitSegment &seg = loop.segments[q];
        const int pa = joints[q], pb = joints[(q + 1) % n];
        if(!seg.arc) {
            const int id = s.addLine(pa, pb);
            curves.push_back(id);
            const Vec2 d = s.pointPos(pb) - s.pointPos(pa);
            const double ang = std::fabs(std::atan2(d.y, d.x));
            if(std::min(ang, kPi - ang) < level) suggested.push_back({SkCon::Horizontal, id});
            else if(std::fabs(ang - kPi / 2) < level) suggested.push_back({SkCon::Vertical, id});
            continue;
        }
        const Vec2 c = map(seg.centre), a = s.pointPos(pa), b = s.pointPos(pb), m = map(seg.mid);
        const int pc = s.addPoint(c.x, c.y);
        // Counter-clockwise in the sketch: a -> mid -> b turns left.
        const bool ccw = (m - a).cross(b - m) > 0;
        curves.push_back(ccw ? s.addArc(pc, pa, pb) : s.addArc(pc, pb, pa));
    }
    // Smooth joins: tangent where a line and an arc (or two arcs) meet
    // without a visible corner.
    const double tangent = tangentDegrees * kPi / 180.0;
    auto direction = [&](size_t q, bool atEnd) { // travel direction of segment q at its end / start
        const FitSegment &seg = loop.segments[q];
        const Vec2 a = s.pointPos(joints[q]), b = s.pointPos(joints[(q + 1) % n]);
        if(!seg.arc) return (b - a).normalized();
        const SkEntity &arc = *s.find(curves[q]);
        const Vec2 c = s.pointPos(arc.a);
        const Vec2 p = atEnd ? b : a;
        Vec2 t = (p - c).perp().normalized(); // CCW tangent
        const Vec2 m = map(seg.mid);
        if((m - a).cross(b - m) < 0) t = -t; // the outline runs clockwise here
        return t;
    };
    for(size_t q = 0; q < n; ++q) {
        const size_t r = (q + 1) % n;
        if(!loop.segments[q].arc && !loop.segments[r].arc) continue;
        const Vec2 d1 = direction(q, true), d2 = direction(r, false);
        const double ang = std::acos(std::clamp(d1.dot(d2), -1.0, 1.0));
        if(ang < tangent) suggested.push_back({SkCon::Tangent, curves[q], curves[r]});
    }
    return curves;
}

} // namespace cad
