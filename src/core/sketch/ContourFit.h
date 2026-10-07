#pragma once

#include "base/Vec2.h"
#include "sketch/Sketch.h"

#include <functional>
#include <vector>

namespace cad {

// Turns a traced outline (a closed polyline, e.g. from a photo) into a few
// clean sketch curves: straight lines and circular arcs within `tolerance`
// of the outline, or one circle.

struct FitSegment {
    bool arc = false;
    Vec2 a, b;     // start and end, along the outline
    Vec2 mid;      // arcs: a point half way along
    Vec2 centre;   // arcs
    double radius = 0.0;
};

struct FitLoop {
    bool circle = false;
    Vec2 centre;          // circle
    double radius = 0.0;  // circle
    std::vector<FitSegment> segments; // otherwise, joined end to start, closing the loop
};

FitLoop fitContour(const std::vector<Vec2> &loop, double tolerance);

// Least-squares circle through points (false: they are about in line).
bool fitCircle(const std::vector<Vec2> &pts, Vec2 &centre, double &radius);

// A constraint the traced geometry suggests (level and plumb lines, smooth
// joins between lines and arcs); kept only if the sketch still solves.
struct SuggestedConstraint {
    SkCon type;
    int e1 = 0, e2 = 0;
};

// Adds a fitted loop to a sketch, mapping its points with `map` (e.g. photo
// pixels to sketch millimetres). Lines within `levelDegrees` of level or plumb
// are suggested horizontal / vertical; lines and arcs meeting within
// `tangentDegrees` are suggested tangent (a traced arc's ends fall a little
// short of the true tangent points, so the solver finishes the join).
// Returns the new curve ids.
std::vector<int> addFittedLoop(Sketch &sketch, const FitLoop &loop, const std::function<Vec2(Vec2)> &map,
                               std::vector<SuggestedConstraint> &suggested, double levelDegrees = 1.5,
                               double tangentDegrees = 25.0);

} // namespace cad
