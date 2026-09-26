#include "topo/Refs.h"

namespace cad {

json TopoRef::toJson() const {
    return json{{"body", body}, {"kind", cad::toString(kind)}, {"name", name}, {"sig", sig.toJson()}};
}

TopoRef TopoRef::fromJson(const json &j) {
    TopoRef r;
    if(!j.is_object()) return r;
    r.body = jget<std::string>(j, "body", "");
    r.kind = topoKindFromString(jget<std::string>(j, "kind", "face"));
    r.name = jget<std::string>(j, "name", "");
    r.sig = GeomSig::fromJson(j.value("sig", json()));
    return r;
}

namespace {

const char *planeKindName(PlaneRef::Kind k) {
    switch(k) {
    case PlaneRef::Kind::XY: return "xy";
    case PlaneRef::Kind::XZ: return "xz";
    case PlaneRef::Kind::YZ: return "yz";
    case PlaneRef::Kind::Construction: return "construction";
    case PlaneRef::Kind::Face: return "face";
    }
    return "xy";
}

PlaneRef::Kind planeKindFromName(const std::string &s) {
    if(s == "xz") return PlaneRef::Kind::XZ;
    if(s == "yz") return PlaneRef::Kind::YZ;
    if(s == "construction") return PlaneRef::Kind::Construction;
    if(s == "face") return PlaneRef::Kind::Face;
    return PlaneRef::Kind::XY;
}

} // namespace

json PlaneRef::toJson() const {
    json j{{"kind", planeKindName(kind)}};
    if(kind == Kind::Construction) j["plane"] = plane;
    if(kind == Kind::Face) j["face"] = face.toJson();
    return j;
}

PlaneRef PlaneRef::fromJson(const json &j) {
    PlaneRef r;
    if(!j.is_object()) return r;
    r.kind = planeKindFromName(jget<std::string>(j, "kind", "xy"));
    r.plane = jget<int>(j, "plane", kNoFeature);
    if(j.contains("face")) r.face = TopoRef::fromJson(j["face"]);
    return r;
}

json ProfileRef::toJson() const {
    json j{{"sketch", sketch}, {"key", key}, {"sample", cad::toJson(sample)}};
    auto poly = [](const std::vector<Vec2> &pts) {
        json a = json::array();
        for(const Vec2 &p : pts) a.push_back(cad::toJson(p));
        return a;
    };
    if(!outline.empty()) {
        j["outline"] = poly(outline);
        json hs = json::array();
        for(const auto &h : holes) hs.push_back(poly(h));
        j["holes"] = hs;
    }
    return j;
}

ProfileRef ProfileRef::fromJson(const json &j) {
    ProfileRef r;
    if(!j.is_object()) return r;
    r.sketch = jget<int>(j, "sketch", kNoFeature);
    r.key = jget<std::string>(j, "key", "");
    r.sample = vec2FromJson(j.value("sample", json()));
    auto poly = [](const json &a) {
        std::vector<Vec2> pts;
        if(a.is_array())
            for(const auto &p : a) pts.push_back(vec2FromJson(p));
        return pts;
    };
    r.outline = poly(j.value("outline", json()));
    for(const auto &h : j.value("holes", json::array())) r.holes.push_back(poly(h));
    return r;
}

} // namespace cad
