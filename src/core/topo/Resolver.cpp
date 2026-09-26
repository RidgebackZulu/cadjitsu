#include "topo/Resolver.h"

#include "geom/OcctUtil.h"

#include <TopoDS.hxx>

#include <limits>
#include <set>
#include <sstream>

namespace cad {

namespace {

// Similarity threshold for accepting a candidate found by base name / geometry.
constexpr double kBaseNameThreshold = 1.5;
constexpr double kGeometryThreshold = 0.35;

int bestBySignature(const Body &body, TopoKind kind, const std::vector<int> &candidates, const GeomSig &sig,
                    double scale, double &bestScore) {
    int best = 0;
    bestScore = std::numeric_limits<double>::infinity();
    for(int idx : candidates) {
        if(kind == TopoKind::Edge && !body.shape.isSelectableEdge(idx)) continue;
        const double s = signatureDistance(sig, signatureOf(body.shape.shapeOf(kind, idx)), scale);
        if(s < bestScore) {
            bestScore = s;
            best = idx;
        }
    }
    return best;
}

std::string shortName(const std::string &n) { return n.size() > 60 ? n.substr(0, 57) + "..." : n; }

} // namespace

TopoRef makeTopoRef(const Body &body, TopoKind kind, int index) {
    TopoRef r;
    r.body = body.id;
    r.kind = kind;
    r.name = body.shape.nameOf(kind, index);
    r.sig = signatureOf(body.shape.shapeOf(kind, index));
    return r;
}

ResolvedRef resolveRef(const ModelState &state, const TopoRef &ref) {
    ResolvedRef out;
    if(ref.empty()) {
        out.status = Status::error("empty reference");
        return out;
    }
    const char *kindName = toString(ref.kind);

    // Candidate bodies: the referenced one (following joins) first, then all others.
    std::vector<const Body *> bodies;
    if(const Body *b = state.body(ref.body)) bodies.push_back(b);
    for(const Body *b : state.orderedBodies())
        if(std::find(bodies.begin(), bodies.end(), b) == bodies.end()) bodies.push_back(b);

    auto accept = [&](const Body *b, int idx, Status st) {
        out.ok = true;
        out.body = b;
        out.index = idx;
        out.shape = b->shape.shapeOf(ref.kind, idx);
        out.status = std::move(st);
        return out;
    };

    // 1. Exact name.
    for(const Body *b : bodies) {
        const int idx = b->shape.indexOfName(ref.kind, ref.name);
        if(idx > 0) return accept(b, idx, Status::ok());
    }
    // 2. Alias (faces absorbed into a merged face).
    if(ref.kind == TopoKind::Face) {
        for(const Body *b : bodies) {
            auto hits = b->shape.indicesWithAlias(ref.name);
            if(hits.size() == 1) return accept(b, hits[0], Status::ok());
            if(hits.size() > 1) {
                double score;
                const int best = bestBySignature(*b, ref.kind, hits, ref.sig, state.modelSize(), score);
                if(best) return accept(b, best, Status::ok());
            }
        }
    }
    const double scale = state.modelSize();
    // 3. Same base name (entity was split or renumbered): closest signature.
    const std::string base = baseName(ref.name);
    {
        const Body *bestBody = nullptr;
        int bestIdx = 0;
        double bestScore = std::numeric_limits<double>::infinity();
        for(const Body *b : bodies) {
            auto cands = b->shape.indicesWithBaseName(ref.kind, base);
            if(cands.empty()) continue;
            double s;
            const int idx = bestBySignature(*b, ref.kind, cands, ref.sig, scale, s);
            if(idx && (s < bestScore || !ref.sig.valid())) {
                bestScore = s;
                bestBody = b;
                bestIdx = idx;
            }
        }
        if(bestBody && (bestScore < kBaseNameThreshold || !ref.sig.valid()))
            return accept(bestBody, bestIdx,
                          Status::warning(std::string("re-resolved ") + kindName + " '" + shortName(ref.name) + "'"));
    }
    // 4. Geometry only.
    if(ref.sig.valid()) {
        const Body *bestBody = nullptr;
        int bestIdx = 0;
        double bestScore = std::numeric_limits<double>::infinity();
        for(const Body *b : bodies) {
            std::vector<int> all;
            for(int i = 1; i <= b->shape.count(ref.kind); ++i) all.push_back(i);
            double s;
            const int idx = bestBySignature(*b, ref.kind, all, ref.sig, scale, s);
            if(idx && s < bestScore) {
                bestScore = s;
                bestBody = b;
                bestIdx = idx;
            }
        }
        if(bestBody && bestScore < kGeometryThreshold)
            return accept(bestBody, bestIdx,
                          Status::warning(std::string("re-resolved ") + kindName + " by geometry"));
    }
    out.status = Status::error(std::string("lost reference to ") + kindName + " '" + shortName(ref.name) + "'");
    return out;
}

gp_Ax3 originPlaneFrame(PlaneRef::Kind kind) {
    switch(kind) {
    case PlaneRef::Kind::XZ: return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    case PlaneRef::Kind::YZ: return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    case PlaneRef::Kind::XY:
    default: return gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    }
}

bool resolvePlane(const ModelState &state, const PlaneRef &ref, gp_Ax3 &frame, Status &status) {
    switch(ref.kind) {
    case PlaneRef::Kind::XY:
    case PlaneRef::Kind::XZ:
    case PlaneRef::Kind::YZ:
        frame = originPlaneFrame(ref.kind);
        return true;
    case PlaneRef::Kind::Construction: {
        auto it = state.planes.find(ref.plane);
        if(it == state.planes.end()) {
            status.merge(Status::error("construction plane is missing"));
            return false;
        }
        frame = it->second->frame;
        return true;
    }
    case PlaneRef::Kind::Face: {
        ResolvedRef r = resolveRef(state, ref.face);
        status.merge(r.status);
        if(!r.ok) return false;
        gp_Pln pln;
        if(!planeOfFace(TopoDS::Face(r.shape), pln)) {
            status.merge(Status::error("the selected face is not planar"));
            return false;
        }
        frame = frameForPlane(pln);
        return true;
    }
    }
    return false;
}

const Profile *resolveProfile(const ModelState &state, const ProfileRef &ref, const SketchResult *&sketch,
                              Status &status) {
    auto it = state.sketches.find(ref.sketch);
    if(it == state.sketches.end()) {
        status.merge(Status::error("the profile's sketch is missing"));
        return nullptr;
    }
    sketch = it->second.get();
    if(const Profile *p = sketch->profileByKey(ref.key)) return p;

    // Region containing the original sample point.
    for(const auto &p : sketch->profiles) {
        if(p.contains(ref.sample)) {
            status.merge(Status::warning("profile re-resolved"));
            return &p;
        }
    }
    // Most similar set of bounding segments (by curve).
    auto curvesOf = [](const std::string &key) {
        std::set<std::string> out;
        std::stringstream ss(key);
        std::string item;
        while(std::getline(ss, item, ',')) out.insert(item.substr(0, item.find('.')));
        return out;
    };
    const auto want = curvesOf(ref.key);
    const Profile *best = nullptr;
    double bestScore = 0.0;
    for(const auto &p : sketch->profiles) {
        const auto have = curvesOf(p.key);
        size_t common = 0;
        for(const auto &c : want) common += have.count(c);
        const double jaccard = double(common) / double(want.size() + have.size() - common);
        if(jaccard > bestScore) {
            bestScore = jaccard;
            best = &p;
        }
    }
    if(best && bestScore >= 0.5) {
        status.merge(Status::warning("profile re-resolved"));
        return best;
    }
    status.merge(Status::error("lost a profile of " + sketch->name));
    return nullptr;
}

} // namespace cad
