#pragma once

#include "base/Ids.h"
#include "base/Json.h"
#include "base/Vec2.h"
#include "topo/NamedShape.h"

#include <string>
#include <vector>

namespace cad {

// A persistent reference to a face / edge / vertex of a body.
struct TopoRef {
    BodyId body;
    TopoKind kind = TopoKind::Face;
    std::string name;
    GeomSig sig;

    bool empty() const { return name.empty(); }
    bool operator==(const TopoRef &o) const { return body == o.body && kind == o.kind && name == o.name; }

    json toJson() const;
    static TopoRef fromJson(const json &j);
};

// Where a sketch or construction plane sits.
struct PlaneRef {
    enum class Kind { XY, XZ, YZ, Construction, Face };
    Kind kind = Kind::XY;
    FeatureId plane = kNoFeature; // Construction
    TopoRef face;                 // Face

    static PlaneRef origin(Kind k) {
        PlaneRef r;
        r.kind = k;
        return r;
    }
    static PlaneRef construction(FeatureId id) {
        PlaneRef r;
        r.kind = Kind::Construction;
        r.plane = id;
        return r;
    }
    static PlaneRef onFace(const TopoRef &f) {
        PlaneRef r;
        r.kind = Kind::Face;
        r.face = f;
        return r;
    }

    json toJson() const;
    static PlaneRef fromJson(const json &j);
};

// A closed region of a sketch, identified by the sketch segments that bound it.
struct ProfileRef {
    ProfileRef() = default;
    ProfileRef(FeatureId s, std::string k, Vec2 p) : sketch(s), key(std::move(k)), sample(p) {}

    FeatureId sketch = kNoFeature;
    std::string key;
    Vec2 sample; // a point inside the region when it was picked (fallback)
    // The region's shape when it was picked (sketch coordinates): if later
    // sketch curves split it, every piece inside this outline (and outside
    // its holes) is still the region. Empty in designs from before.
    std::vector<Vec2> outline;
    std::vector<std::vector<Vec2>> holes;

    json toJson() const;
    static ProfileRef fromJson(const json &j);
};

} // namespace cad
