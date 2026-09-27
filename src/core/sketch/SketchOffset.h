#pragma once

#include "sketch/Sketch.h"
#include "sketch/SketchSolver.h"

#include <string>
#include <vector>

namespace cad {

// Sketch Offset: copies curves a distance to one side, joined up again at
// their corners, and constrained to follow the originals at one offset
// dimension.

// One curve of a chain, in the chain's running direction (a line's a -> b, an
// arc's start -> end, unless reversed).
struct ChainLink {
    int id = 0;
    bool reversed = false;
};

// Curves joined end to end, in order. A lone circle is a closed chain of one.
struct CurveChain {
    std::vector<ChainLink> links;
    bool closed = false;
};

// The curves joined to `curveId` end to end through points that join exactly
// two curves (shared points, or points held together by a coincident
// constraint), `curveId` included. A branching point ends the chain there.
std::vector<int> connectedCurves(const Sketch &sketch, int curveId);

// The line, arc or circle nearest `p` (0 if the sketch has none), and how far.
int nearestCurve(const Sketch &sketch, Vec2 p, double *distance = nullptr);

// Orders the given curves (lines, arcs, circles) into chains, closed ones
// running counter-clockwise (their inside on the left). Fails when a point
// joins more than two of them.
bool buildChains(const Sketch &sketch, const std::vector<int> &curveIds, std::vector<CurveChain> &chains,
                 std::string &error);

// An offset curve's geometry.
struct OffsetCurve {
    SkType type = SkType::Line; // Line, Arc or Circle
    int source = 0;             // the curve it is offset from
    bool construction = false;
    Vec2 start, end;            // in the chain's direction (lines and arcs)
    Vec2 centre;                // arcs and circles
    double radius = 0.0;
    bool reversed = false;      // an arc running clockwise along the chain
};

struct OffsetChain {
    std::vector<OffsetCurve> curves;
    bool closed = false;
};

// The chains offset by `distance` to their left (negative: to their right).
// Neighbouring curves are extended or trimmed to meet. Fails, naming the
// curve, when the offset is too big for one of them.
bool offsetGeometry(const Sketch &sketch, const std::vector<CurveChain> &chains, double distance,
                    std::vector<OffsetChain> &out, std::string &error);

// The sign of the distance that offsets a closed chain outwards (away from
// what it encloses); +1 (to the left) for an open chain.
double outwardSign(const Sketch &sketch, const CurveChain &chain);

// The signed distance from `p` to the nearest curve of the chains, positive on
// the left: where the mouse puts the offset.
double sideDistance(const Sketch &sketch, const std::vector<CurveChain> &chains, Vec2 p);

// Points along an offset curve (for previews).
std::vector<Vec2> offsetPolyline(const OffsetCurve &curve);

struct OffsetApplied {
    std::vector<int> entities; // new curves and points
    int dimension = 0;         // the offset dimension (0: none could be added)
};

// Adds the chains offset by `distance` (to their left) to the sketch, with
// constraints that keep them there: lines parallel at the offset distance,
// arcs and circles on the same centre at the offset radius, tangent where the
// originals were. The first offset dimension gets parameter `param` with
// expression `expr`; the others take their value from it. Constraints that
// would over-constrain the sketch are left out; `lookup` gives the other
// dimensions' values for that check.
bool applyOffset(Sketch &sketch, const std::vector<CurveChain> &chains, double distance, const std::string &param,
                 const std::string &expr, const DimensionLookup &lookup, OffsetApplied &out, std::string &error);

} // namespace cad
