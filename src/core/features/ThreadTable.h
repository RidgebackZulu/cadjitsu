#pragma once

#include <string>
#include <vector>

namespace cad {

// A standard thread size (all in mm).
struct ThreadSpec {
    std::string name;     // "M6", "M8x1", "1/4-20 UNC"
    std::string standard; // "ISO", "ISO fine", "UNC", "UNF"
    double major = 0.0;   // nominal (major) diameter
    double pitch = 0.0;
    double tapDrill = 0.0;
    double minor() const { return major - 1.0825318 * pitch; } // basic minor diameter (internal, D1)
    // Small threads print poorly: by default they are left for a tap (or a
    // self-tapping screw, or a heat-set insert) instead of being modelled.
    bool modelByDefault() const { return major >= 4.8; }
};

const std::vector<ThreadSpec> &threadTable();
const ThreadSpec *findThread(const std::string &name);
// The size a cylinder of `diameter` most likely is: for a hole, the nearest
// tap drill (or minor diameter); for a boss, the nearest major diameter.
// `standard` empty = any; coarse sizes win ties.
const ThreadSpec *nearestThread(double diameter, bool internal, const std::string &standard = {});

} // namespace cad
