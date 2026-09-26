#pragma once

#include "sketch/Sketch.h"

#include <functional>
#include <string>
#include <vector>

namespace cad {

struct SolveOutcome {
    bool ok = true;
    bool redundant = false;          // solved, but some constraints repeat others
    std::string message;
    int dof = -1;                    // remaining degrees of freedom (-1 = not computed)
    std::vector<int> failed;         // constraints that could not be satisfied
    std::vector<int> freeEntities;   // entities that are not fully constrained
};

// Looks up the current value of a dimension parameter (mm / rad).
using DimensionLookup = std::function<bool(const std::string &param, double &value)>;

struct SolveOptions {
    std::vector<int> dragged;   // point entities the user is dragging (kept near their position)
    bool computeFreeEntities = true;
};

// Solves the sketch's constraints in place, moving its geometry.
SolveOutcome solveSketch(Sketch &sketch, const DimensionLookup &lookup, const SolveOptions &options = {});

} // namespace cad
