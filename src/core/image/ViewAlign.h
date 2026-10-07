#pragma once

#include "base/Vec2.h"
#include "image/RegionTrace.h"

#include <optional>
#include <string>
#include <vector>

namespace cad {

// Two or three photos (or drawings) of a part, from the front, the right side
// and the top, set up as canvases on the XZ, YZ and XY planes at one scale:
// each picture's part (its bounding box in the picture) is sized from one
// dimension measured for real, and placed so the views line up as a
// projection. The part stands on z = 0, centred on the Z axis.
//
// The front view (on XZ, seen from -Y) shows X across and Z up; the right
// side view (YZ, seen from +X) Y across and Z up; the top view (XY, from +Z)
// X across and Y up.
enum class ViewSide { Front, Side, Top };

struct ViewInput {
    ViewSide side = ViewSide::Front;
    int width = 0, height = 0; // the picture's size (pixels)
    PixelBox box;              // the part in it (pixels)
};

struct ViewPlacement {
    ViewSide side = ViewSide::Front;
    double mmPerPixel = 0.0;
    Vec2 origin; // where the picture's centre goes on its plane (mm)
};

struct ViewsResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings; // views that disagree with each other
    double size[3] = {0, 0, 0};       // the part's X, Y and Z size (mm)
    std::vector<ViewPlacement> views; // in the order given
};

// `axis` (0: X width, 1: Y depth, 2: Z height) measures `mm` for real; it
// must show in at least one of the views.
ViewsResult alignViews(const std::vector<ViewInput> &views, int axis, double mm);

// The plane of a view: "xz", "yz" or "xy".
const char *viewPlaneName(ViewSide side);

} // namespace cad
