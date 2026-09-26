#pragma once

#include "doc/Document.h"

namespace cadly {

// A 60 x 40 x 12 mm plate with a through hole, a counterbored hole, a
// filleted corner and a boss: exercises most display paths.
void buildDemoBracket(cad::Document &doc);

} // namespace cadly
