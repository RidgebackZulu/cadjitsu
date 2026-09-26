#include "sketch/SketchSolver.h"

namespace cad {

// Constraint solving arrives with the libslvs mapping (milestone M3a). Until
// then sketches keep the geometry exactly as drawn.
SolveOutcome solveSketch(Sketch &, const DimensionLookup &, const SolveOptions &) { return {}; }

} // namespace cad
