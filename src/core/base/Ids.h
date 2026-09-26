#pragma once

#include <string>

namespace cad {

// Timeline features are numbered 1, 2, 3... for the lifetime of a document.
using FeatureId = int;
constexpr FeatureId kNoFeature = 0;

// Bodies are identified by the feature that created them ("b7"), with ".k"
// suffixes when one feature produces several solids.
using BodyId = std::string;

inline BodyId bodyIdFor(FeatureId fid) { return "b" + std::to_string(fid); }

} // namespace cad
