#pragma once

#include "base/Json.h"
#include "base/Vec2.h"
#include "topo/Refs.h"

#include <array>
#include <optional>
#include <string>

namespace cad {

// A reference picture placed on a plane (Insert > Canvas): a photo of a part
// to trace over in a sketch, scaled to its true size. It lives in the browser
// and the document, not in the timeline, and follows its plane (or face).
//
// The picture's pixels map onto the plane as follows: its centre is at
// `origin` (plane coordinates, mm), one pixel is `mmPerPixel` mm, it is
// turned `rotation` degrees anticlockwise and mirrored left-right if `flip`.
// Pixel (u, v) counts from the top-left corner, v downwards.
//
// A perspective correction turns a photo taken at an angle into a true top
// view: `corners` are four pixels of the original photo that are the corners
// of something rectangular in reality (top-left, top-right, bottom-right,
// bottom-left), `realWidth` x `realHeight` mm. The app warps the photo so
// that rectangle is square and true to scale; `pixelWidth` / `pixelHeight`
// are the size of the picture as shown (after any correction).
struct ReferenceImage {
    int id = 0;
    std::string name;          // "Canvas1"
    PlaneRef plane;
    std::string imageKey;      // the photo's bytes in the document (Document::image)
    int pixelWidth = 0, pixelHeight = 0;
    Vec2 origin;
    double mmPerPixel = 0.1;
    double rotation = 0.0;     // degrees
    bool flip = false;
    double opacity = 0.5;
    bool visible = true;

    struct Perspective {
        std::array<Vec2, 4> corners; // in the original photo's pixels: TL, TR, BR, BL
        double realWidth = 0.0, realHeight = 0.0;
    };
    std::optional<Perspective> perspective;

    json toJson() const;
    static ReferenceImage fromJson(const json &j);

    // Pixel <-> plane coordinates (mm) for the picture as shown.
    Vec2 toPlane(Vec2 pixel) const;
    Vec2 toPixel(Vec2 plane) const;
    // The picture's corners on the plane: top-left, top-right, bottom-right, bottom-left.
    std::array<Vec2, 4> planeCorners() const;
    // The picture's width on the plane (mm).
    double width() const { return pixelWidth * mmPerPixel; }
};

// Scales the picture about plane point `p1` so plane points `p1` and `p2`
// (clicked on two marks of the photo) end up `realDistance` mm apart.
bool calibrateReferenceImage(ReferenceImage &image, Vec2 p1, Vec2 p2, double realDistance, std::string &error);

} // namespace cad
