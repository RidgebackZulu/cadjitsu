#pragma once

#include "base/Vec2.h"
#include "image/RegionTrace.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace cad {

// Radial lens distortion (barrel or pincushion), as phone and wide cameras
// bend straight edges near the frame's sides. A point at distance r from the
// picture's centre (in units of half its diagonal) is seen at r (1 + k1 r^2 +
// k2 r^4): k1 < 0 is barrel (edges bow out), k1 > 0 pincushion.
struct LensDistortion {
    double k1 = 0.0, k2 = 0.0;
    bool none() const { return k1 == 0.0 && k2 == 0.0; }
};

// Where an undistorted pixel is seen in the photo (`width` x `height` pixels).
Vec2 distortPixel(const LensDistortion &lens, Vec2 p, int width, int height);
// Where a pixel of the photo is in the undistorted picture.
Vec2 undistortPixel(const LensDistortion &lens, Vec2 p, int width, int height);

// Plumb-line calibration: `lines` are points clicked along edges that are
// straight in reality (photo pixels, three or more each). The distortion that
// makes them straightest; k2 too when there are points enough. Empty if they
// do not tell (fewer than three points on any line) or no sensible lens does.
std::optional<LensDistortion> estimateLens(const std::vector<std::vector<Vec2>> &lines, int width, int height);

// How far the points of `lines` are from straight, worst line, in pixels
// (after undistorting with `lens`).
double lineStraightness(const LensDistortion &lens, const std::vector<std::vector<Vec2>> &lines, int width, int height);

// The photo (RGBA, 8 bits a channel) undistorted, the same size; pixels seen
// from outside the photo are transparent.
std::vector<std::uint8_t> undistortImage(const ImageView &photo, const LensDistortion &lens);

} // namespace cad
