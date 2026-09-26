#include "doc/Section.h"

#include "topo/Resolver.h"

namespace cad {

json SectionAnalysis::toJson() const {
    return json{{"id", id}, {"name", name}, {"plane", plane.toJson()}, {"offset", offset}, {"flip", flip},
                {"visible", visible}};
}

SectionAnalysis SectionAnalysis::fromJson(const json &j) {
    SectionAnalysis s;
    s.id = jget<int>(j, "id", 0);
    s.name = jget<std::string>(j, "name", "");
    if(j.contains("plane")) s.plane = PlaneRef::fromJson(j.at("plane"));
    s.offset = jget<double>(j, "offset", 0.0);
    s.flip = jget<bool>(j, "flip", false);
    s.visible = jget<bool>(j, "visible", true);
    return s;
}

bool resolveSection(const ModelState &state, const SectionAnalysis &s, gp_Pln &plane, Status &status) {
    gp_Ax3 frame;
    if(!resolvePlane(state, s.plane, frame, status)) return false;
    frame.Translate(gp_Vec(frame.Direction()) * s.offset);
    // Fusion cuts away the side the plane's normal points to.
    if(s.flip) frame.ZReverse();
    plane = gp_Pln(frame);
    return true;
}

} // namespace cad
