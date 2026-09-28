#pragma once

#include "doc/ModelState.h"
#include "topo/Refs.h"

#include <QVector3D>

#include <array>
#include <optional>

namespace cadjitsu {

using Quad = std::array<QVector3D, 4>;

// The square a construction plane is drawn (and picked) as.
Quad constructionPlaneQuad(const cad::PlaneResult &plane);
// An origin plane is drawn over its positive quadrant, `size` on a side.
Quad originPlaneQuad(cad::PlaneRef::Kind kind, float size);
// Where a ray (origin, direction) meets a flat convex quad, as a ray parameter.
std::optional<float> rayQuad(const QVector3D &origin, const QVector3D &direction, const Quad &quad, bool orthographic);

} // namespace cadjitsu
