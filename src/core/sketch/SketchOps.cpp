#include "sketch/SketchOps.h"

#include "sketch/SketchText.h"

#include <cmath>
#include <map>
#include <set>

namespace cad {

namespace {

// What a selection copies: its curves and texts, and every point they use.
struct Selection {
    std::vector<int> curves, texts;
    std::vector<int> points; // in sketch order
};

Selection gather(const Sketch &s, const std::vector<int> &ids) {
    const std::set<int> want(ids.begin(), ids.end());
    std::set<int> points;
    Selection out;
    for(const SkEntity &e : s.entities) {
        if(!want.count(e.id)) continue;
        if(e.isCurve()) {
            out.curves.push_back(e.id);
            for(int p : {e.a, e.b, e.c})
                if(p > 0) points.insert(p);
        } else if(e.isText()) {
            out.texts.push_back(e.id);
        } else if(e.type == SkType::Point) {
            points.insert(e.id);
        }
    }
    for(const SkEntity &e : s.entities)
        if(e.type == SkType::Point && points.count(e.id)) out.points.push_back(e.id);
    return out;
}

double tolerance(Vec2 p) { return 1e-7 * std::max(1.0, p.length()); }

// A copy of `e` with new ids for what it uses; not projected any more.
SkEntity copied(const SkEntity &e, const std::map<int, int> &pts) {
    SkEntity c = e;
    c.projSketch = c.projEntity = 0;
    for(int *p : {&c.a, &c.b, &c.c}) {
        auto it = pts.find(*p);
        if(it != pts.end()) *p = it->second;
    }
    return c;
}

int addCopy(Sketch &s, SkEntity e) {
    e.id = s.nextId++;
    s.entities.push_back(e);
    return e.id;
}

} // namespace

bool mirrorEntities(Sketch &sketch, const std::vector<int> &ids, int lineId, std::vector<int> &created,
                    std::string &error) {
    created.clear();
    // The mirror line.
    Vec2 p0, dir;
    if(lineId == kSketchXAxis) dir = {1, 0};
    else if(lineId == kSketchYAxis) dir = {0, 1};
    else {
        const SkEntity *l = sketch.find(lineId);
        if(!l || l->type != SkType::Line) {
            error = "pick a line (or an axis) to mirror about";
            return false;
        }
        p0 = sketch.pointPos(l->a);
        dir = (sketch.pointPos(l->b) - p0).normalized();
        if(dir.length() < 0.5) {
            error = "the mirror line has no length";
            return false;
        }
    }
    auto reflect = [&](Vec2 p) {
        const Vec2 v = p - p0;
        return p0 + dir * (2.0 * v.dot(dir)) - v;
    };
    auto onLine = [&](Vec2 p) { return std::abs((p - p0).cross(dir)) < tolerance(p); };

    std::vector<int> want;
    for(int id : ids)
        if(id != lineId && id > 0) want.push_back(id);
    const Selection sel = gather(sketch, want);
    if(sel.curves.empty() && sel.texts.empty() && sel.points.empty()) {
        error = "select what to mirror";
        return false;
    }

    Sketch work = sketch;
    std::map<int, int> pts; // original point -> its mirror image (itself on the line)
    std::vector<std::pair<int, int>> symmetric;
    for(int pid : sel.points) {
        const Vec2 p = work.pointPos(pid);
        if(onLine(p)) {
            pts[pid] = pid;
            continue;
        }
        const Vec2 q = reflect(p);
        const int id = work.addPoint(q.x, q.y, work.find(pid)->construction);
        pts[pid] = id;
        created.push_back(id);
        symmetric.emplace_back(pid, id);
    }
    for(int cid : sel.curves) {
        const SkEntity e = *work.find(cid);
        SkEntity c = copied(e, pts);
        if(e.type == SkType::Line && c.a == e.a && c.b == e.b) continue; // lies on the mirror line
        if(e.type == SkType::Arc) std::swap(c.b, c.c); // a mirrored arc runs the other way
        const int id = addCopy(work, c);
        created.push_back(id);
        if(e.type == SkType::Circle) work.addConstraint(SkCon::Equal, e.id, id);
    }
    const double lineAngle = std::atan2(dir.y, dir.x) * 180.0 / kPi;
    for(int tid : sel.texts) {
        const SkEntity e = *work.find(tid);
        // Mirroring turns the letters over: the text reads the other way
        // (mirror flag), and the flag flips it about its middle, so the
        // origin moves along the new baseline by twice that middle.
        double middle = 0.0;
        if(const auto shape = buildText(e.text, textStyleOf(e)); shape && shape->ok)
            middle = 0.5 * (shape->min.x + shape->max.x);
        const double angle = 2.0 * lineAngle - e.angle + 180.0;
        const double rad = angle * kPi / 180.0;
        const Vec2 o = reflect(work.pointPos(e.a)) - Vec2(std::cos(rad), std::sin(rad)) * (2.0 * middle);
        SkEntity c = e;
        c.projSketch = c.projEntity = 0;
        c.a = work.addPoint(o.x, o.y);
        c.mirror = !e.mirror;
        c.angle = std::fmod(angle + 720.0, 360.0);
        created.push_back(c.a);
        created.push_back(addCopy(work, c));
    }
    for(const auto &[p, q] : symmetric) work.addConstraint(SkCon::Symmetric, p, q, lineId);
    if(created.empty()) {
        error = "everything selected lies on the mirror line";
        return false;
    }
    sketch = std::move(work);
    return true;
}

double patternStep(int count, double totalAngle) {
    if(count < 2) return 0.0;
    const bool full = std::abs(std::abs(totalAngle) - 360.0) < 1e-9;
    return full ? totalAngle / count : totalAngle / (count - 1);
}

bool patternEntities(Sketch &sketch, const std::vector<int> &ids, Vec2 centre, int count, double totalAngle,
                     std::vector<int> &created, std::string &error) {
    created.clear();
    if(count < 2 || count > 360) {
        error = "the number of copies must be 2 to 360";
        return false;
    }
    if(std::abs(totalAngle) < 1e-9 || std::abs(totalAngle) > 360.0 + 1e-9) {
        error = "the angle must be more than 0 and at most 360 degrees";
        return false;
    }
    std::vector<int> want;
    for(int id : ids)
        if(id > 0) want.push_back(id);
    const Selection sel = gather(sketch, want);
    if(sel.curves.empty() && sel.texts.empty() && sel.points.empty()) {
        error = "select what to copy around the centre";
        return false;
    }
    std::set<int> inSelection(sel.curves.begin(), sel.curves.end());
    inSelection.insert(sel.texts.begin(), sel.texts.end());
    inSelection.insert(sel.points.begin(), sel.points.end());
    for(int t : sel.texts) inSelection.insert(sketch.find(t)->a);

    Sketch work = sketch;
    const std::vector<SkConstraint> originals = sketch.constraints;
    const double step = patternStep(count, totalAngle);
    for(int k = 1; k < count; ++k) {
        const double deg = step * k, rad = deg * kPi / 180.0;
        const double c = std::cos(rad), s = std::sin(rad);
        auto rotate = [&](Vec2 p) {
            const Vec2 v = p - centre;
            return centre + Vec2(v.x * c - v.y * s, v.x * s + v.y * c);
        };
        std::map<int, int> map; // original -> copy (points at the centre map to themselves)
        auto copyPoint = [&](int pid) {
            const Vec2 p = work.pointPos(pid);
            if(distance(p, centre) < tolerance(p)) {
                map[pid] = pid;
                return;
            }
            const Vec2 q = rotate(p);
            const int id = work.addPoint(q.x, q.y, work.find(pid)->construction);
            map[pid] = id;
            created.push_back(id);
        };
        for(int pid : sel.points) copyPoint(pid);
        for(int cid : sel.curves) {
            const int id = addCopy(work, copied(*work.find(cid), map));
            map[cid] = id;
            created.push_back(id);
        }
        for(int tid : sel.texts) {
            SkEntity t = *work.find(tid);
            if(!map.count(t.a)) copyPoint(t.a);
            t = copied(t, map);
            t.angle = std::fmod(t.angle + deg + 720.0, 360.0);
            const int id = addCopy(work, t);
            map[tid] = id;
            created.push_back(id);
        }
        // The constraints among the copied entities (some lose their meaning
        // when turned: horizontal / vertical survive only quarter turns).
        const double quarter = std::fmod(std::abs(deg), 90.0);
        const bool quarterTurn = quarter < 1e-9 || 90.0 - quarter < 1e-9;
        const bool swapHV = quarterTurn && (int(std::lround(std::abs(deg) / 90.0)) % 2 == 1);
        for(const SkConstraint &o : originals) {
            bool inside = true, moved = false;
            for(int e : {o.e1, o.e2, o.e3}) {
                if(e == 0) continue;
                auto it = map.find(e);
                if(it == map.end() || !inSelection.count(e)) inside = false;
                else if(it->second != e) moved = true;
            }
            if(!inside || !moved || o.type == SkCon::Fix || o.driven) continue;
            SkConstraint n = o;
            n.id = work.nextId++;
            for(int *e : {&n.e1, &n.e2, &n.e3})
                if(*e) *e = map[*e];
            if(o.type == SkCon::Horizontal || o.type == SkCon::Vertical || o.type == SkCon::HDistance ||
               o.type == SkCon::VDistance) {
                if(!quarterTurn) continue;
                if(swapHV) {
                    n.type = o.type == SkCon::Horizontal ? SkCon::Vertical
                           : o.type == SkCon::Vertical   ? SkCon::Horizontal
                           : o.type == SkCon::HDistance  ? SkCon::VDistance
                                                         : SkCon::HDistance;
                }
            }
            if(isDimension(o.type)) {
                // The copy follows the original's value.
                n.valueFrom = o.valueFrom ? o.valueFrom : o.id;
                n.param.clear();
                n.expr.clear();
                n.label = rotate(o.label);
            }
            work.constraints.push_back(n);
        }
    }
    sketch = std::move(work);
    return true;
}

} // namespace cad
