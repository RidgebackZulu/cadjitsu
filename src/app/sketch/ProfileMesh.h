#pragma once

#include "sketch/ProfileBuilder.h"

#include <gp_Ax3.hxx>

#include <QVector3D>

#include <vector>

namespace cadjitsu {

// Triangles covering a sketch profile (Fusion's profile shading), in world
// coordinates. Empty if the profile cannot be turned into a face.
std::vector<QVector3D> triangulateProfile(const cad::Profile &profile, const gp_Ax3 &frame);

} // namespace cadjitsu
