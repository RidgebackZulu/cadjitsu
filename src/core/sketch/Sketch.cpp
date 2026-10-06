#include "sketch/Sketch.h"

#include <algorithm>
#include <set>

namespace cad {

namespace {

struct ConName {
    SkCon type;
    const char *name;
};

const ConName kConNames[] = {
    {SkCon::Coincident, "coincident"},
    {SkCon::PointOnCurve, "pointOnCurve"},
    {SkCon::Horizontal, "horizontal"},
    {SkCon::Vertical, "vertical"},
    {SkCon::Parallel, "parallel"},
    {SkCon::Perpendicular, "perpendicular"},
    {SkCon::Tangent, "tangent"},
    {SkCon::Equal, "equal"},
    {SkCon::Midpoint, "midpoint"},
    {SkCon::Concentric, "concentric"},
    {SkCon::Symmetric, "symmetric"},
    {SkCon::Fix, "fix"},
    {SkCon::Distance, "distance"},
    {SkCon::HDistance, "hdistance"},
    {SkCon::VDistance, "vdistance"},
    {SkCon::PointLineDistance, "pointLineDistance"},
    {SkCon::Radius, "radius"},
    {SkCon::Diameter, "diameter"},
    {SkCon::Angle, "angle"},
    {SkCon::OffsetRadius, "offsetRadius"},
};

const char *typeName(SkType t) {
    switch(t) {
    case SkType::Point: return "point";
    case SkType::Line: return "line";
    case SkType::Circle: return "circle";
    case SkType::Arc: return "arc";
    case SkType::Text: return "text";
    }
    return "point";
}

SkType typeFromName(const std::string &s) {
    if(s == "line") return SkType::Line;
    if(s == "circle") return SkType::Circle;
    if(s == "arc") return SkType::Arc;
    if(s == "text") return SkType::Text;
    return SkType::Point;
}

} // namespace

bool isDimension(SkCon t) {
    switch(t) {
    case SkCon::Distance:
    case SkCon::HDistance:
    case SkCon::VDistance:
    case SkCon::PointLineDistance:
    case SkCon::Radius:
    case SkCon::Diameter:
    case SkCon::Angle:
    case SkCon::OffsetRadius:
        return true;
    default:
        return false;
    }
}

const char *toString(SkCon t) {
    for(const auto &c : kConNames)
        if(c.type == t) return c.name;
    return "coincident";
}

bool skConFromString(const std::string &s, SkCon &out) {
    for(const auto &c : kConNames) {
        if(s == c.name) {
            out = c.type;
            return true;
        }
    }
    return false;
}

SkEntity *Sketch::find(int id) {
    for(auto &e : entities)
        if(e.id == id) return &e;
    return nullptr;
}

const SkEntity *Sketch::find(int id) const {
    for(const auto &e : entities)
        if(e.id == id) return &e;
    return nullptr;
}

const SkConstraint *Sketch::findConstraint(int id) const {
    for(const auto &c : constraints)
        if(c.id == id) return &c;
    return nullptr;
}

SkConstraint *Sketch::findConstraint(int id) {
    for(auto &c : constraints)
        if(c.id == id) return &c;
    return nullptr;
}

Vec2 Sketch::pointPos(int pointId) const {
    const SkEntity *p = find(pointId);
    return p ? Vec2(p->x, p->y) : Vec2();
}

int Sketch::addPoint(double x, double y, bool construction) {
    SkEntity e;
    e.id = nextId++;
    e.type = SkType::Point;
    e.x = x;
    e.y = y;
    e.construction = construction;
    entities.push_back(e);
    return e.id;
}

int Sketch::addLine(int p1, int p2, bool construction) {
    SkEntity e;
    e.id = nextId++;
    e.type = SkType::Line;
    e.a = p1;
    e.b = p2;
    e.construction = construction;
    entities.push_back(e);
    return e.id;
}

int Sketch::addLine(Vec2 a, Vec2 b, bool construction) {
    const int p1 = addPoint(a.x, a.y);
    const int p2 = addPoint(b.x, b.y);
    return addLine(p1, p2, construction);
}

int Sketch::addCircle(int centre, double radius, bool construction) {
    SkEntity e;
    e.id = nextId++;
    e.type = SkType::Circle;
    e.a = centre;
    e.r = radius;
    e.construction = construction;
    entities.push_back(e);
    return e.id;
}

int Sketch::addCircle(Vec2 centre, double radius, bool construction) {
    return addCircle(addPoint(centre.x, centre.y), radius, construction);
}

int Sketch::addArc(int centre, int start, int end, bool construction) {
    SkEntity e;
    e.id = nextId++;
    e.type = SkType::Arc;
    e.a = centre;
    e.b = start;
    e.c = end;
    e.construction = construction;
    entities.push_back(e);
    return e.id;
}

int Sketch::addText(int origin, const std::string &str, bool construction) {
    SkEntity e;
    e.id = nextId++;
    e.type = SkType::Text;
    e.a = origin;
    e.text = str;
    e.construction = construction;
    entities.push_back(e);
    return e.id;
}

int Sketch::addText(Vec2 origin, const std::string &str, bool construction) {
    return addText(addPoint(origin.x, origin.y, construction), str, construction);
}

std::vector<int> Sketch::addRectangle(Vec2 c1, Vec2 c2, bool construction) {
    const double x0 = std::min(c1.x, c2.x), x1 = std::max(c1.x, c2.x);
    const double y0 = std::min(c1.y, c2.y), y1 = std::max(c1.y, c2.y);
    const int p00 = addPoint(x0, y0), p10 = addPoint(x1, y0);
    const int p11 = addPoint(x1, y1), p01 = addPoint(x0, y1);
    const int bottom = addLine(p00, p10, construction);
    const int right = addLine(p10, p11, construction);
    const int top = addLine(p11, p01, construction);
    const int left = addLine(p01, p00, construction);
    addConstraint(SkCon::Horizontal, bottom);
    addConstraint(SkCon::Horizontal, top);
    addConstraint(SkCon::Vertical, right);
    addConstraint(SkCon::Vertical, left);
    return {bottom, right, top, left};
}

int Sketch::addConstraint(SkCon type, int e1, int e2, int e3, const std::string &param) {
    SkConstraint c;
    c.id = nextId++;
    c.type = type;
    c.e1 = e1;
    c.e2 = e2;
    c.e3 = e3;
    c.param = param;
    constraints.push_back(c);
    return c.id;
}

void Sketch::removeConstraint(int id) {
    constraints.erase(std::remove_if(constraints.begin(), constraints.end(),
                                     [&](const SkConstraint &c) { return c.id == id; }),
                      constraints.end());
    dropOrphans();
}

void Sketch::dropOrphans() {
    // Dimensions whose value came from a constraint that is gone.
    for(;;) {
        const auto it = std::find_if(constraints.begin(), constraints.end(), [&](const SkConstraint &c) {
            return c.valueFrom && !findConstraint(c.valueFrom);
        });
        if(it == constraints.end()) return;
        constraints.erase(it);
    }
}

void Sketch::removeEntity(int id) {
    const SkEntity *victim = find(id);
    if(!victim) return;
    std::set<int> removed{id};
    std::vector<int> candidatePoints;
    if(victim->type != SkType::Point) {
        for(int p : {victim->a, victim->b, victim->c})
            if(p) candidatePoints.push_back(p);
    } else {
        // Removing a point removes every curve that uses it.
        for(const auto &e : entities)
            if(e.type != SkType::Point && (e.a == id || e.b == id || e.c == id)) removed.insert(e.id);
        for(const auto &e : entities) {
            if(removed.count(e.id) && e.type != SkType::Point) {
                for(int p : {e.a, e.b, e.c})
                    if(p && p != id) candidatePoints.push_back(p);
            }
        }
    }
    entities.erase(std::remove_if(entities.begin(), entities.end(),
                                  [&](const SkEntity &e) { return removed.count(e.id) > 0; }),
                   entities.end());
    // Drop points that no remaining curve uses.
    for(int p : candidatePoints) {
        bool used = false;
        for(const auto &e : entities)
            if(e.type != SkType::Point && (e.a == p || e.b == p || e.c == p)) used = true;
        if(!used) {
            entities.erase(std::remove_if(entities.begin(), entities.end(),
                                          [&](const SkEntity &e) { return e.id == p; }),
                           entities.end());
            removed.insert(p);
        }
    }
    constraints.erase(std::remove_if(constraints.begin(), constraints.end(),
                                     [&](const SkConstraint &c) {
                                         return removed.count(c.e1) || removed.count(c.e2) ||
                                                removed.count(c.e3);
                                     }),
                      constraints.end());
    dropOrphans();
}

double Sketch::arcRadius(const SkEntity &arc) const {
    return distance(pointPos(arc.a), pointPos(arc.b));
}

json Sketch::toJson() const {
    json ents = json::array();
    for(const auto &e : entities) {
        json je{{"id", e.id}, {"type", typeName(e.type)}};
        if(e.construction) je["construction"] = true;
        if(e.projSketch) je["projected"] = json::array({e.projSketch, e.projEntity});
        switch(e.type) {
        case SkType::Point:
            je["x"] = e.x;
            je["y"] = e.y;
            break;
        case SkType::Line:
            je["a"] = e.a;
            je["b"] = e.b;
            break;
        case SkType::Circle:
            je["a"] = e.a;
            je["r"] = e.r;
            break;
        case SkType::Arc:
            je["a"] = e.a;
            je["b"] = e.b;
            je["c"] = e.c;
            break;
        case SkType::Text:
            je["a"] = e.a;
            je["text"] = e.text;
            je["font"] = e.font;
            je["size"] = e.size;
            if(e.angle != 0.0) je["angle"] = e.angle;
            if(e.bold) je["bold"] = true;
            if(e.italic) je["italic"] = true;
            if(e.mirror) je["mirror"] = true;
            break;
        }
        ents.push_back(std::move(je));
    }
    json cons = json::array();
    for(const auto &c : constraints) {
        json jc{{"id", c.id}, {"type", toString(c.type)}, {"e1", c.e1}};
        if(c.e2) jc["e2"] = c.e2;
        if(c.e3) jc["e3"] = c.e3;
        if(!c.param.empty()) jc["param"] = c.param;
        if(!c.expr.empty()) jc["expr"] = c.expr;
        if(c.driven) jc["driven"] = true;
        if(c.supplementary) jc["supplementary"] = true;
        if(c.valueFrom) jc["valueFrom"] = c.valueFrom;
        if(isDimension(c.type)) jc["label"] = cad::toJson(c.label);
        cons.push_back(std::move(jc));
    }
    return json{{"entities", ents}, {"constraints", cons}, {"nextId", nextId}};
}

Sketch Sketch::fromJson(const json &j) {
    Sketch s;
    s.nextId = jget<int>(j, "nextId", 1);
    for(const auto &je : j.value("entities", json::array())) {
        SkEntity e;
        e.id = jget<int>(je, "id", 0);
        e.type = typeFromName(jget<std::string>(je, "type", "point"));
        e.construction = jget<bool>(je, "construction", false);
        if(je.contains("projected") && je["projected"].is_array() && je["projected"].size() == 2) {
            e.projSketch = je["projected"][0].get<int>();
            e.projEntity = je["projected"][1].get<int>();
        }
        e.x = jget<double>(je, "x", 0.0);
        e.y = jget<double>(je, "y", 0.0);
        e.a = jget<int>(je, "a", 0);
        e.b = jget<int>(je, "b", 0);
        e.c = jget<int>(je, "c", 0);
        e.r = jget<double>(je, "r", 0.0);
        if(e.type == SkType::Text) {
            e.text = jget<std::string>(je, "text", "");
            e.font = jget<std::string>(je, "font", "DejaVu Sans");
            e.size = jget<double>(je, "size", 5.0);
            e.angle = jget<double>(je, "angle", 0.0);
            e.bold = jget<bool>(je, "bold", false);
            e.italic = jget<bool>(je, "italic", false);
            e.mirror = jget<bool>(je, "mirror", false);
        }
        s.entities.push_back(e);
        s.nextId = std::max(s.nextId, e.id + 1);
    }
    for(const auto &jc : j.value("constraints", json::array())) {
        SkConstraint c;
        c.id = jget<int>(jc, "id", 0);
        if(!skConFromString(jget<std::string>(jc, "type", ""), c.type)) continue;
        c.e1 = jget<int>(jc, "e1", 0);
        c.e2 = jget<int>(jc, "e2", 0);
        c.e3 = jget<int>(jc, "e3", 0);
        c.param = jget<std::string>(jc, "param", "");
        c.expr = jget<std::string>(jc, "expr", "");
        c.driven = jget<bool>(jc, "driven", false);
        c.supplementary = jget<bool>(jc, "supplementary", false);
        c.valueFrom = jget<int>(jc, "valueFrom", 0);
        if(jc.contains("label")) c.label = vec2FromJson(jc["label"]);
        s.constraints.push_back(c);
        s.nextId = std::max(s.nextId, c.id + 1);
    }
    return s;
}

} // namespace cad
