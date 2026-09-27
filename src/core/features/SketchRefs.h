#pragma once

#include "doc/Document.h"

namespace cad {

// After sketch `sketch` was edited: extrudes using its regions get their
// stored inside point and outline refreshed from the regions as they are now
// (where the region is still found by its key), without an undo step of their
// own. So when a later edit renumbers the sketch's curves, the fallbacks look
// where the region is, not where it was first drawn. Returns how many
// features changed.
int refreshProfileRefs(Document &doc, FeatureId sketch);

} // namespace cad
