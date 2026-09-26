#include "sketch/SketchSolver.h"

#include "sketch/SlvsLock.h"

#include <slvs.h>

#include <array>
#include <cmath>
#include <map>
#include <set>

namespace cad {

namespace {

constexpr Slvs_hGroup kFixed = 1;
constexpr Slvs_hGroup kSolve = 2;

// Translates a Sketch into a libslvs system (and back).
class SlvsSystem {
public:
    SlvsSystem(const Sketch &sketch, const DimensionLookup &lookup) : m_sketch(sketch), m_lookup(lookup) {
        buildFixed();
        buildEntities();
        buildConstraints();
    }

    // Extra constraints used by the degree-of-freedom analysis.
    void addWhereDragged(int pointId) {
        auto it = m_entity.find(pointId);
        if(it == m_entity.end()) return;
        con(SLVS_C_WHERE_DRAGGED, 0.0, it->second, 0, 0, 0, 0);
    }
    void addFixedDiameter(int circleId) {
        const SkEntity *e = m_sketch.find(circleId);
        auto it = m_entity.find(circleId);
        if(!e || it == m_entity.end()) return;
        con(SLVS_C_DIAMETER, 2.0 * radiusOf(*e), 0, 0, it->second, 0, 0);
    }

    struct Result {
        int code = SLVS_RESULT_OKAY;
        int dof = -1;
        std::vector<int> failed; // sketch constraint ids
    };

    Result solve(const std::vector<int> &dragged, bool findFailed) {
        std::vector<Slvs_hParam> drag;
        for(int id : dragged) {
            auto it = m_pointParams.find(id);
            if(it == m_pointParams.end()) continue;
            drag.push_back(it->second[0]);
            drag.push_back(it->second[1]);
        }
        std::vector<Slvs_hConstraint> failed(m_constraints.size() + 1);
        Slvs_System sys = {};
        sys.param = m_params.data();
        sys.params = int(m_params.size());
        sys.entity = m_entities.data();
        sys.entities = int(m_entities.size());
        sys.constraint = m_constraints.data();
        sys.constraints = int(m_constraints.size());
        sys.dragged = drag.empty() ? nullptr : drag.data();
        sys.ndragged = int(drag.size());
        sys.calculateFaileds = findFailed ? 1 : 0;
        sys.failed = failed.data();
        sys.faileds = int(failed.size());
        Slvs_Solve(&sys, kSolve);

        Result r;
        r.code = sys.result;
        r.dof = sys.dof;
        std::set<int> seen;
        for(int i = 0; i < sys.faileds && i < int(failed.size()); ++i) {
            auto it = m_owner.find(failed[size_t(i)]);
            if(it != m_owner.end() && it->second > 0 && seen.insert(it->second).second) r.failed.push_back(it->second);
        }
        return r;
    }

    // Copies solved values into `out`.
    void writeBack(Sketch &out) const {
        auto val = [&](Slvs_hParam h) { return m_params[m_paramIndex.at(h)].val; };
        for(auto &e : out.entities) {
            if(e.type == SkType::Point) {
                auto it = m_pointParams.find(e.id);
                if(it == m_pointParams.end()) continue;
                e.x = val(it->second[0]);
                e.y = val(it->second[1]);
            } else if(e.type == SkType::Circle) {
                auto it = m_radiusParam.find(e.id);
                if(it != m_radiusParam.end()) e.r = val(it->second);
            }
        }
    }

    const std::vector<std::string> &warnings() const { return m_warnings; }

private:
    const Sketch &m_sketch;
    const DimensionLookup &m_lookup;
    std::vector<Slvs_Param> m_params;
    std::map<Slvs_hParam, size_t> m_paramIndex;
    std::vector<Slvs_Entity> m_entities;
    std::vector<Slvs_Constraint> m_constraints;
    std::map<int, Slvs_hEntity> m_entity;                 // sketch entity id -> slvs entity
    std::map<int, std::array<Slvs_hParam, 2>> m_pointParams;
    std::map<int, Slvs_hParam> m_radiusParam;
    std::map<Slvs_hConstraint, int> m_owner;              // slvs constraint -> sketch constraint
    std::vector<std::string> m_warnings;
    Slvs_hParam m_nextParam = 1;
    Slvs_hEntity m_nextEntity = 1;
    Slvs_hConstraint m_nextCon = 1;
    Slvs_hEntity m_workplane = 0, m_normal = 0;
    int m_currentOwner = 0;

    Slvs_hParam param(Slvs_hGroup g, double v) {
        m_paramIndex[m_nextParam] = m_params.size();
        m_params.push_back(Slvs_MakeParam(m_nextParam, g, v));
        return m_nextParam++;
    }
    Slvs_hEntity addEntity(Slvs_Entity e) {
        m_entities.push_back(e);
        return e.h;
    }
    Slvs_hEntity point2d(Slvs_hGroup g, double u, double v, std::array<Slvs_hParam, 2> *ps = nullptr) {
        const Slvs_hParam pu = param(g, u), pv = param(g, v);
        if(ps) *ps = {pu, pv};
        return addEntity(Slvs_MakePoint2d(m_nextEntity++, g, m_workplane, pu, pv));
    }
    void con(int type, double val, Slvs_hEntity ptA, Slvs_hEntity ptB, Slvs_hEntity eA, Slvs_hEntity eB, int other,
             int other2 = 0) {
        Slvs_Constraint c = Slvs_MakeConstraint(m_nextCon, kSolve, type, m_workplane, val, ptA, ptB, eA, eB);
        c.other = other;
        c.other2 = other2;
        m_owner[m_nextCon] = m_currentOwner;
        ++m_nextCon;
        m_constraints.push_back(c);
    }

    double radiusOf(const SkEntity &e) const {
        if(e.type == SkType::Circle) return e.r;
        return distance(m_sketch.pointPos(e.a), m_sketch.pointPos(e.b));
    }
    Vec2 centreOf(const SkEntity &e) const { return m_sketch.pointPos(e.a); }

    void buildFixed() {
        const Slvs_hParam ox = param(kFixed, 0), oy = param(kFixed, 0), oz = param(kFixed, 0);
        const Slvs_hEntity origin3d = addEntity(Slvs_MakePoint3d(m_nextEntity++, kFixed, ox, oy, oz));
        double qw, qx, qy, qz;
        Slvs_MakeQuaternion(1, 0, 0, 0, 1, 0, &qw, &qx, &qy, &qz);
        const Slvs_hParam p0 = param(kFixed, qw), p1 = param(kFixed, qx), p2 = param(kFixed, qy), p3 = param(kFixed, qz);
        m_normal = addEntity(Slvs_MakeNormal3d(m_nextEntity++, kFixed, p0, p1, p2, p3));
        m_workplane = addEntity(Slvs_MakeWorkplane(m_nextEntity++, kFixed, origin3d, m_normal));

        const Slvs_hEntity o = point2d(kFixed, 0, 0);
        const Slvs_hEntity xEnd = point2d(kFixed, 1, 0);
        const Slvs_hEntity yEnd = point2d(kFixed, 0, 1);
        m_entity[kSketchOrigin] = o;
        m_entity[kSketchXAxis] = addEntity(Slvs_MakeLineSegment(m_nextEntity++, kFixed, m_workplane, o, xEnd));
        m_entity[kSketchYAxis] = addEntity(Slvs_MakeLineSegment(m_nextEntity++, kFixed, m_workplane, o, yEnd));
    }

    void buildEntities() {
        for(const auto &e : m_sketch.entities) {
            if(e.type != SkType::Point) continue;
            std::array<Slvs_hParam, 2> ps;
            m_entity[e.id] = point2d(kSolve, e.x, e.y, &ps);
            m_pointParams[e.id] = ps;
        }
        for(const auto &e : m_sketch.entities) {
            switch(e.type) {
            case SkType::Point:
                break;
            case SkType::Line:
                if(m_entity.count(e.a) && m_entity.count(e.b))
                    m_entity[e.id] = addEntity(
                        Slvs_MakeLineSegment(m_nextEntity++, kSolve, m_workplane, m_entity[e.a], m_entity[e.b]));
                break;
            case SkType::Circle: {
                if(!m_entity.count(e.a)) break;
                const Slvs_hParam r = param(kSolve, e.r);
                m_radiusParam[e.id] = r;
                const Slvs_hEntity dist = addEntity(Slvs_MakeDistance(m_nextEntity++, kSolve, m_workplane, r));
                m_entity[e.id] =
                    addEntity(Slvs_MakeCircle(m_nextEntity++, kSolve, m_workplane, m_entity[e.a], m_normal, dist));
                break;
            }
            case SkType::Arc:
                if(m_entity.count(e.a) && m_entity.count(e.b) && m_entity.count(e.c))
                    m_entity[e.id] = addEntity(Slvs_MakeArcOfCircle(m_nextEntity++, kSolve, m_workplane, m_normal,
                                                                    m_entity[e.a], m_entity[e.b], m_entity[e.c]));
                break;
            }
        }
    }

    const SkEntity *ent(int id) const { return m_sketch.find(id); }
    bool isPoint(int id) const {
        if(id == kSketchOrigin) return true;
        const SkEntity *e = ent(id);
        return e && e->type == SkType::Point;
    }
    bool isLine(int id) const {
        if(id == kSketchXAxis || id == kSketchYAxis) return true;
        const SkEntity *e = ent(id);
        return e && e->type == SkType::Line;
    }
    bool isRound(int id) const {
        const SkEntity *e = ent(id);
        return e && (e->type == SkType::Circle || e->type == SkType::Arc);
    }
    Slvs_hEntity h(int id) const {
        auto it = m_entity.find(id);
        return it == m_entity.end() ? 0 : it->second;
    }

    bool dimValue(const SkConstraint &c, double &v) {
        if(!m_lookup || !m_lookup(c.param, v)) {
            m_warnings.push_back("dimension " + c.param + " has no valid value");
            return false;
        }
        return true;
    }

    // Tangency between a line / circle / arc without a shared endpoint, via a
    // hidden touching point: on both curves, with the radius to it perpendicular
    // to the line (or collinear with the other centre).
    void hiddenTangent(int curveA, int curveB) {
        const bool aLine = isLine(curveA);
        const int line = aLine ? curveA : (isLine(curveB) ? curveB : 0);
        const int round1 = aLine ? curveB : curveA;
        const int round2 = line ? 0 : curveB;
        const SkEntity &r1 = *ent(round1);
        const Vec2 c1 = centreOf(r1);
        Vec2 start;
        if(line) {
            const SkEntity *l = ent(line);
            Vec2 a(0, 0), b(1, 0);
            if(line == kSketchYAxis) b = Vec2(0, 1);
            if(l) {
                a = m_sketch.pointPos(l->a);
                b = m_sketch.pointPos(l->b);
            }
            const Vec2 d = (b - a).normalized();
            start = a + d * (c1 - a).dot(d);
        } else {
            const Vec2 c2 = centreOf(*ent(round2));
            start = c1 + (c2 - c1).normalized() * radiusOf(r1);
        }
        const Slvs_hEntity p = point2d(kSolve, start.x, start.y);
        con(SLVS_C_PT_ON_CIRCLE, 0, p, 0, h(round1), 0, 0);
        if(line) {
            con(SLVS_C_PT_ON_LINE, 0, p, 0, h(line), 0, 0);
            const Slvs_hEntity radial =
                addEntity(Slvs_MakeLineSegment(m_nextEntity++, kSolve, m_workplane, h(r1.a), p));
            con(SLVS_C_PERPENDICULAR, 0, 0, 0, radial, h(line), 0);
        } else {
            con(SLVS_C_PT_ON_CIRCLE, 0, p, 0, h(round2), 0, 0);
            const Slvs_hEntity centres =
                addEntity(Slvs_MakeLineSegment(m_nextEntity++, kSolve, m_workplane, h(r1.a), h(ent(round2)->a)));
            con(SLVS_C_PT_ON_LINE, 0, p, 0, centres, 0, 0);
        }
    }

    void buildConstraint(const SkConstraint &c) {
        m_currentOwner = c.id;
        const int a = c.e1, b = c.e2;
        switch(c.type) {
        case SkCon::Coincident:
        case SkCon::PointOnCurve: {
            int pt = a, other = b;
            if(!isPoint(pt)) std::swap(pt, other);
            if(!isPoint(pt) || !h(pt) || !h(other)) break;
            if(isPoint(other)) con(SLVS_C_POINTS_COINCIDENT, 0, h(pt), h(other), 0, 0, 0);
            else if(isLine(other)) con(SLVS_C_PT_ON_LINE, 0, h(pt), 0, h(other), 0, 0);
            else if(isRound(other)) con(SLVS_C_PT_ON_CIRCLE, 0, h(pt), 0, h(other), 0, 0);
            break;
        }
        case SkCon::Horizontal:
        case SkCon::Vertical: {
            const int type = c.type == SkCon::Horizontal ? SLVS_C_HORIZONTAL : SLVS_C_VERTICAL;
            if(isLine(a) && h(a)) con(type, 0, 0, 0, h(a), 0, 0);
            else if(isPoint(a) && isPoint(b) && h(a) && h(b)) con(type, 0, h(a), h(b), 0, 0, 0);
            break;
        }
        case SkCon::Parallel:
            if(isLine(a) && isLine(b)) con(SLVS_C_PARALLEL, 0, 0, 0, h(a), h(b), 0);
            break;
        case SkCon::Perpendicular:
            if(isLine(a) && isLine(b)) con(SLVS_C_PERPENDICULAR, 0, 0, 0, h(a), h(b), 0);
            break;
        case SkCon::Tangent: {
            const SkEntity *ea = ent(a), *eb = ent(b);
            if(!ea || !eb) break;
            const SkEntity *arc = nullptr, *line = nullptr;
            if(ea->type == SkType::Arc && eb->type == SkType::Line) arc = ea, line = eb;
            if(eb->type == SkType::Arc && ea->type == SkType::Line) arc = eb, line = ea;
            if(arc && line && (line->a == arc->b || line->b == arc->b || line->a == arc->c || line->b == arc->c)) {
                const bool atEnd = line->a == arc->c || line->b == arc->c;
                con(SLVS_C_ARC_LINE_TANGENT, 0, 0, 0, h(arc->id), h(line->id), atEnd ? 1 : 0);
                break;
            }
            if(ea->type == SkType::Arc && eb->type == SkType::Arc) {
                auto endOf = [](const SkEntity &x, int p) { return p == x.c ? 1 : 0; };
                int shared = 0;
                for(int p : {ea->b, ea->c})
                    if(p == eb->b || p == eb->c) shared = p;
                if(shared) {
                    con(SLVS_C_CURVE_CURVE_TANGENT, 0, 0, 0, h(a), h(b), endOf(*ea, shared), endOf(*eb, shared));
                    break;
                }
            }
            if((isLine(a) && isRound(b)) || (isRound(a) && isLine(b)) || (isRound(a) && isRound(b))) hiddenTangent(a, b);
            break;
        }
        case SkCon::Equal:
            if(isLine(a) && isLine(b)) con(SLVS_C_EQUAL_LENGTH_LINES, 0, 0, 0, h(a), h(b), 0);
            else if(isRound(a) && isRound(b)) con(SLVS_C_EQUAL_RADIUS, 0, 0, 0, h(a), h(b), 0);
            else if(isLine(a) && ent(b) && ent(b)->type == SkType::Arc) con(SLVS_C_EQUAL_LINE_ARC_LEN, 0, 0, 0, h(a), h(b), 0);
            else if(isLine(b) && ent(a) && ent(a)->type == SkType::Arc) con(SLVS_C_EQUAL_LINE_ARC_LEN, 0, 0, 0, h(b), h(a), 0);
            break;
        case SkCon::Midpoint: {
            int pt = a, line = b;
            if(!isPoint(pt)) std::swap(pt, line);
            if(isPoint(pt) && isLine(line)) con(SLVS_C_AT_MIDPOINT, 0, h(pt), 0, h(line), 0, 0);
            break;
        }
        case SkCon::Concentric:
            if(isRound(a) && isRound(b)) con(SLVS_C_POINTS_COINCIDENT, 0, h(ent(a)->a), h(ent(b)->a), 0, 0, 0);
            break;
        case SkCon::Symmetric:
            if(isPoint(a) && isPoint(b) && isLine(c.e3)) con(SLVS_C_SYMMETRIC_LINE, 0, h(a), h(b), h(c.e3), 0, 0);
            break;
        case SkCon::Fix: {
            const SkEntity *e = ent(a);
            if(!e) break;
            if(e->type == SkType::Point) con(SLVS_C_WHERE_DRAGGED, 0, h(a), 0, 0, 0, 0);
            else if(e->type == SkType::Line) {
                con(SLVS_C_WHERE_DRAGGED, 0, h(e->a), 0, 0, 0, 0);
                con(SLVS_C_WHERE_DRAGGED, 0, h(e->b), 0, 0, 0, 0);
            } else if(e->type == SkType::Circle) {
                con(SLVS_C_WHERE_DRAGGED, 0, h(e->a), 0, 0, 0, 0);
                con(SLVS_C_DIAMETER, 2 * e->r, 0, 0, h(a), 0, 0);
            } else {
                for(int p : {e->a, e->b, e->c}) con(SLVS_C_WHERE_DRAGGED, 0, h(p), 0, 0, 0, 0);
            }
            break;
        }
        case SkCon::Distance: {
            double v;
            if(!dimValue(c, v)) break;
            if(isLine(a) && !b) {
                const SkEntity *l = ent(a);
                if(l) con(SLVS_C_PT_PT_DISTANCE, std::fabs(v), h(l->a), h(l->b), 0, 0, 0);
            } else if(isPoint(a) && isPoint(b)) {
                con(SLVS_C_PT_PT_DISTANCE, std::fabs(v), h(a), h(b), 0, 0, 0);
            } else if(isPoint(a) && isLine(b)) {
                buildPointLine(a, b, v);
            } else if(isLine(a) && isPoint(b)) {
                buildPointLine(b, a, v);
            }
            break;
        }
        case SkCon::HDistance:
        case SkCon::VDistance: {
            double v;
            if(!dimValue(c, v)) break;
            int pa = a, pb = b;
            if(isLine(a) && !b) {
                const SkEntity *l = ent(a);
                if(!l) break;
                pa = l->a;
                pb = l->b;
            }
            if(!isPoint(pa) || !isPoint(pb)) break;
            const bool hz = c.type == SkCon::HDistance;
            const Vec2 da = pa == kSketchOrigin ? Vec2() : m_sketch.pointPos(pa);
            const Vec2 db = pb == kSketchOrigin ? Vec2() : m_sketch.pointPos(pb);
            const double cur = hz ? db.x - da.x : db.y - da.y;
            // libslvs projects (pB - pA) onto the axis line's vector, which runs
            // from its second point to its first: (-1, 0) / (0, -1) here.
            con(SLVS_C_PROJ_PT_DISTANCE, cur < 0 ? std::fabs(v) : -std::fabs(v), h(pa), h(pb),
                h(hz ? kSketchXAxis : kSketchYAxis), 0, 0);
            break;
        }
        case SkCon::PointLineDistance: {
            double v;
            if(!dimValue(c, v)) break;
            int pt = a, line = b;
            if(!isPoint(pt)) std::swap(pt, line);
            if(isPoint(pt) && isLine(line)) buildPointLine(pt, line, v);
            break;
        }
        case SkCon::Radius:
        case SkCon::Diameter: {
            double v;
            if(!dimValue(c, v) || !isRound(a)) break;
            con(SLVS_C_DIAMETER, c.type == SkCon::Radius ? 2 * std::fabs(v) : std::fabs(v), 0, 0, h(a), 0, 0);
            break;
        }
        case SkCon::Angle: {
            double v;
            if(!dimValue(c, v) || !isLine(a) || !isLine(b)) break;
            con(SLVS_C_ANGLE, v * 180.0 / kPi, 0, 0, h(a), h(b), c.supplementary ? 1 : 0);
            break;
        }
        }
    }

    void buildPointLine(int pt, int line, double v) {
        // Keep the point on its current side of the line.
        const SkEntity *l = ent(line);
        const Vec2 p = pt == kSketchOrigin ? Vec2() : m_sketch.pointPos(pt);
        Vec2 a, b;
        if(line == kSketchXAxis) a = {0, 0}, b = {1, 0};
        else if(line == kSketchYAxis) a = {0, 0}, b = {0, 1};
        else if(l) a = m_sketch.pointPos(l->a), b = m_sketch.pointPos(l->b);
        // libslvs' signed distance is positive on the right of a -> b.
        const double side = (b - a).cross(p - a);
        con(SLVS_C_PT_LINE_DISTANCE, side < 0 ? std::fabs(v) : -std::fabs(v), h(pt), 0, h(line), 0, 0);
    }

    void buildConstraints() {
        for(const auto &c : m_sketch.constraints) {
            if(c.driven) continue;
            const size_t before = m_constraints.size();
            buildConstraint(c);
            if(m_constraints.size() == before)
                m_warnings.push_back(std::string("ignored invalid ") + toString(c.type) + " constraint");
        }
        m_currentOwner = 0;
    }
};

std::vector<int> pointIds(const Sketch &s) {
    std::vector<int> out;
    for(const auto &e : s.entities)
        if(e.type == SkType::Point) out.push_back(e.id);
    return out;
}

} // namespace

SolveOutcome solveSketch(Sketch &sketch, const DimensionLookup &lookup, const SolveOptions &options) {
    SolveOutcome out;
    std::lock_guard<std::mutex> lock(slvsMutex());

    SlvsSystem system(sketch, lookup);
    const SlvsSystem::Result r = system.solve(options.dragged, true);
    out.dof = r.dof;
    out.failed = r.failed;
    for(const auto &w : system.warnings()) out.message += (out.message.empty() ? "" : "; ") + w;

    const bool solved = r.code == SLVS_RESULT_OKAY || r.code == SLVS_RESULT_REDUNDANT_OKAY;
    if(!solved) {
        out.ok = false;
        out.message = r.code == SLVS_RESULT_INCONSISTENT ? "the sketch is over-constrained"
                                                        : "the sketch constraints could not be solved";
        return out;
    }
    system.writeBack(sketch);
    if(r.code == SLVS_RESULT_REDUNDANT_OKAY) {
        out.ok = true;
        if(out.message.empty()) out.message = "the sketch has redundant constraints";
    }

    if(options.computeFreeEntities && out.dof > 0) {
        // A point is fully constrained if pinning it does not remove any
        // degree of freedom; a circle's radius likewise.
        std::set<int> freePoints, freeRadii;
        for(int pid : pointIds(sketch)) {
            SlvsSystem probe(sketch, lookup);
            probe.addWhereDragged(pid);
            const SlvsSystem::Result pr = probe.solve({}, false);
            if(pr.dof >= 0 && pr.dof < out.dof) freePoints.insert(pid);
        }
        for(const auto &e : sketch.entities) {
            if(e.type != SkType::Circle) continue;
            SlvsSystem probe(sketch, lookup);
            probe.addFixedDiameter(e.id);
            const SlvsSystem::Result pr = probe.solve({}, false);
            if(pr.dof >= 0 && pr.dof < out.dof) freeRadii.insert(e.id);
        }
        for(const auto &e : sketch.entities) {
            bool isFree = false;
            switch(e.type) {
            case SkType::Point: isFree = freePoints.count(e.id) > 0; break;
            case SkType::Line: isFree = freePoints.count(e.a) || freePoints.count(e.b); break;
            case SkType::Circle: isFree = freePoints.count(e.a) || freeRadii.count(e.id); break;
            case SkType::Arc: isFree = freePoints.count(e.a) || freePoints.count(e.b) || freePoints.count(e.c); break;
            }
            if(isFree) out.freeEntities.push_back(e.id);
        }
    }
    return out;
}

} // namespace cad
