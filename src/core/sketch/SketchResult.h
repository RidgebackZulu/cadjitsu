#pragma once

#include "base/Ids.h"
#include "base/Status.h"
#include "sketch/ProfileBuilder.h"
#include "sketch/Sketch.h"

#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include <vector>

namespace cad {

// The evaluated form of a sketch feature: solved geometry in its 3D frame and
// the closed profiles available for extrusion.
struct SketchResult {
    FeatureId feature = kNoFeature;
    std::string name;
    gp_Ax3 frame;
    Sketch sketch;                   // solved geometry
    std::vector<Profile> profiles;
    int dof = -1;                    // remaining degrees of freedom (-1 = unknown)
    std::vector<int> failedConstraints;
    std::vector<int> freeEntities;   // entities not fully constrained (drawn blue)
    Status status;

    gp_Pnt toWorld(Vec2 p) const {
        const gp_Dir x = frame.XDirection(), y = frame.YDirection();
        return gp_Pnt(frame.Location().XYZ() + x.XYZ() * p.x + y.XYZ() * p.y);
    }
    Vec2 toSketch(const gp_Pnt &p) const {
        const gp_XYZ d = p.XYZ() - frame.Location().XYZ();
        return {d.Dot(frame.XDirection().XYZ()), d.Dot(frame.YDirection().XYZ())};
    }
    const Profile *profileByKey(const std::string &key) const {
        for(const auto &p : profiles)
            if(p.key == key) return &p;
        return nullptr;
    }
};

} // namespace cad
