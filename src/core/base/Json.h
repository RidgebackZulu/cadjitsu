#pragma once

#include "base/Vec2.h"

#include <nlohmann/json.hpp>

#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_XYZ.hxx>

namespace cad {

using json = nlohmann::json;

inline json toJson(const gp_XYZ &v) { return json::array({v.X(), v.Y(), v.Z()}); }
inline gp_XYZ xyzFromJson(const json &j) {
    if(!j.is_array() || j.size() != 3) return gp_XYZ();
    return gp_XYZ(j[0].get<double>(), j[1].get<double>(), j[2].get<double>());
}

inline json toJson(const Vec2 &v) { return json::array({v.x, v.y}); }
inline Vec2 vec2FromJson(const json &j) {
    if(!j.is_array() || j.size() != 2) return Vec2();
    return Vec2(j[0].get<double>(), j[1].get<double>());
}

inline json toJson(const gp_Ax3 &a) {
    return json{{"origin", toJson(a.Location().XYZ())},
                {"normal", toJson(a.Direction().XYZ())},
                {"xdir", toJson(a.XDirection().XYZ())}};
}
inline gp_Ax3 ax3FromJson(const json &j) {
    const gp_XYZ o = xyzFromJson(j.value("origin", json()));
    gp_XYZ n = xyzFromJson(j.value("normal", json()));
    gp_XYZ x = xyzFromJson(j.value("xdir", json()));
    if(n.Modulus() < 1e-12) n = gp_XYZ(0, 0, 1);
    if(x.Modulus() < 1e-12) x = gp_XYZ(1, 0, 0);
    return gp_Ax3(gp_Pnt(o), gp_Dir(n), gp_Dir(x));
}

template <typename T> T jget(const json &j, const char *key, T fallback) {
    auto it = j.find(key);
    if(it == j.end() || it->is_null()) return fallback;
    try {
        return it->get<T>();
    } catch(const json::exception &) {
        return fallback;
    }
}

} // namespace cad
