#pragma once

#include "sketch/ProfileBuilder.h"

#include <gp_Ax3.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>

#include <string>
#include <utility>
#include <vector>

namespace cad {

struct ProfileFace {
    TopoDS_Face face;
    // Every edge of `face` with the key of the sketch segment it came from.
    std::vector<std::pair<TopoDS_Edge, std::string>> edgeKeys;
};

// Builds a planar OCCT face for a profile placed on `frame`. Consecutive
// pieces of the same sketch curve become one edge (a circle stays one edge).
bool profileToFace(const Profile &profile, const gp_Ax3 &frame, ProfileFace &out, std::string &error);

} // namespace cad
