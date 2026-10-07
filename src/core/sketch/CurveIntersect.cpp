#include "sketch/CurveIntersect.h"

#include "sketch/Sketch.h"

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

constexpr double kTwoPi = 2.0 * kPi;

} // namespace

// ---------------------------------------------------------------------------
// Curve parametrization: lines use t in [0,1]; arcs use the angle offset from
// a0 in [0, sweep]; circles use the absolute angle in [0, 2pi).

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

namespace {

void intersectLineLine(const Curve2 &A, const Curve2 &B, double tol, std::vector<CurveHit> &ha,
                       std::vector<CurveHit> &hb) {
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

void intersectLineCircle(const Curve2 &L, const Curve2 &C, double tol, std::vector<CurveHit> &hl,
                         std::vector<CurveHit> &hc) {
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

void intersectCircleCircle(const Curve2 &A, const Curve2 &B, double tol, std::vector<CurveHit> &ha,
                           std::vector<CurveHit> &hb) {
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

} // namespace

void intersectCurves(const Curve2 &A, const Curve2 &B, double tol, std::vector<CurveHit> &ha, std::vector<CurveHit> &hb) {
    if(!isRound(A) && !isRound(B)) intersectLineLine(A, B, tol, ha, hb);
    else if(!isRound(A)) intersectLineCircle(A, B, tol, ha, hb);
    else if(!isRound(B)) intersectLineCircle(B, A, tol, hb, ha);
    else intersectCircleCircle(A, B, tol, ha, hb);
}


Curve2 curveOfEntity(const Sketch &sketch, const SkEntity &e) {
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
    return c;
}

Vec2 curvePoint(const Curve2 &c, double param) {
    if(c.type == Curve2::Line) return c.p0 + (c.p1 - c.p0) * param;
    const double a = c.type == Curve2::Arc ? c.a0 + param : param;
    return {c.c.x + c.r * std::cos(a), c.c.y + c.r * std::sin(a)};
}

double curveParam(const Curve2 &c, Vec2 p) {
    if(c.type == Curve2::Line) {
        const Vec2 d = c.p1 - c.p0;
        const double l2 = d.length2();
        return l2 > 0 ? (p - c.p0).dot(d) / l2 : 0.0;
    }
    const double a = normAngle((p - c.c).angle());
    if(c.type == Curve2::Circle) return a;
    double off = normAngle(a - c.a0);
    // Past the end: nearer the start (negative) or the end?
    if(off > c.sweep) {
        const double before = kTwoPi - off, after = off - c.sweep;
        if(before < after) off = -before;
    }
    return off;
}

double curveParamEnd(const Curve2 &c) {
    return c.type == Curve2::Line ? 1.0 : c.type == Curve2::Arc ? c.sweep : kTwoPi;
}

} // namespace cad
