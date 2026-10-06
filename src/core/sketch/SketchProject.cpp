#include "sketch/SketchProject.h"

#include <cmath>
#include <set>

namespace cad {

namespace {

// Where a point of `source` falls on the plane `frame` (straight down onto it).
Vec2 onto(const SketchResult &source, Vec2 p, const gp_Ax3 &frame) {
    const gp_XYZ d = source.toWorld(p).XYZ() - frame.Location().XYZ();
    return {d.Dot(frame.XDirection().XYZ()), d.Dot(frame.YDirection().XYZ())};
}

// +1 when the two planes face the same way, -1 when opposite, 0 when tilted.
int parallel(const SketchResult &source, const gp_Ax3 &frame) {
    const double c = source.frame.Direction().Dot(frame.Direction());
    if(c > 1.0 - 1e-9) return 1;
    if(c < -1.0 + 1e-9) return -1;
    return 0;
}

int findProjected(const Sketch &s, FeatureId sketch, int entity) {
    for(const SkEntity &e : s.entities)
        if(e.projSketch == sketch && e.projEntity == entity) return e.id;
    return 0;
}

} // namespace

bool canProject(const SketchResult &source, const SkEntity &e, const gp_Ax3 &frame, std::string *why) {
    if(e.isText()) {
        if(why) *why = "text cannot be projected";
        return false;
    }
    if((e.type == SkType::Circle || e.type == SkType::Arc) && parallel(source, frame) == 0) {
        if(why) *why = "circles and arcs project only onto a parallel plane (on this one they would be ellipses)";
        return false;
    }
    return true;
}

bool projectEntity(Sketch &target, const gp_Ax3 &frame, const SketchResult &source, int entityId,
                   std::vector<int> &created, std::string &error) {
    created.clear();
    const SkEntity *e = source.sketch.find(entityId);
    if(!e) {
        error = "nothing to project";
        return false;
    }
    if(!canProject(source, *e, frame, &error)) return false;
    if(findProjected(target, source.feature, entityId)) return true;

    Sketch work = target;
    auto point = [&](int pid) {
        if(const int have = findProjected(work, source.feature, pid)) return have;
        const Vec2 q = onto(source, source.sketch.pointPos(pid), frame);
        const int id = work.addPoint(q.x, q.y);
        SkEntity &p = *work.find(id);
        p.projSketch = source.feature;
        p.projEntity = pid;
        created.push_back(id);
        return id;
    };
    if(e->type == SkType::Point) {
        point(e->id);
    } else {
        SkEntity c = *e;
        c.construction = false;
        c.a = point(e->a);
        if(e->type == SkType::Line || e->type == SkType::Arc) c.b = point(e->b);
        if(e->type == SkType::Arc) {
            c.c = point(e->c);
            // Seen from the other side, an arc runs the other way.
            if(parallel(source, frame) < 0) std::swap(c.b, c.c);
        }
        c.projSketch = source.feature;
        c.projEntity = e->id;
        c.id = work.nextId++;
        work.entities.push_back(c);
        created.push_back(c.id);
    }
    target = std::move(work);
    return true;
}

void refreshProjections(Sketch &target, const gp_Ax3 &frame,
                        const std::function<const SketchResult *(FeatureId)> &sourceOf,
                        std::vector<std::string> &warnings) {
    std::set<std::string> said;
    auto warn = [&](const std::string &w) {
        if(said.insert(w).second) warnings.push_back(w);
    };
    for(SkEntity &e : target.entities) {
        if(!e.isProjected()) continue;
        const SketchResult *src = sourceOf(e.projSketch);
        if(!src) {
            warn("projected geometry: its sketch is gone");
            continue;
        }
        const SkEntity *from = src->sketch.find(e.projEntity);
        if(!from || from->type != e.type) {
            warn("projected geometry: what it was projected from in " + src->name + " is gone");
            continue;
        }
        if(e.type == SkType::Point) {
            const Vec2 q = onto(*src, Vec2(from->x, from->y), frame);
            e.x = q.x;
            e.y = q.y;
        } else if(e.type == SkType::Circle) {
            e.r = from->r;
        }
        if((e.type == SkType::Circle || e.type == SkType::Arc) && parallel(*src, frame) == 0)
            warn("projected geometry: a circle or arc from " + src->name + " is no longer on a parallel plane");
    }
}

std::vector<FeatureId> projectionSources(const Sketch &target) {
    std::set<FeatureId> ids;
    for(const SkEntity &e : target.entities)
        if(e.isProjected()) ids.insert(e.projSketch);
    return {ids.begin(), ids.end()};
}

} // namespace cad
