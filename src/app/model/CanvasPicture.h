#pragma once

#include "doc/Document.h"
#include "doc/ReferenceImage.h"
#include "image/ViewAlign.h"

#include <QImage>

#include <array>
#include <optional>

namespace cadjitsu {

// The picture a canvas shows: its photo decoded from the document, and
// perspective-corrected when the canvas has a correction. Cached, so the same
// picture keeps the same QImage (and GPU texture) from frame to frame.
QImage canvasPicture(const cad::Document &doc, const cad::ReferenceImage &canvas);

// The photo as stored, decoded (no correction).
QImage canvasPhoto(const cad::Document &doc, const cad::ReferenceImage &canvas);

// The photo with its lens corrected (the photo if it has no lens correction):
// what a perspective correction starts from, the same size as the photo.
QImage canvasSource(const cad::Document &doc, const cad::ReferenceImage &canvas);

// `photo` undistorted for `lens` (the same size).
QImage undistortPicture(const QImage &photo, const cad::LensDistortion &lens);

// Sets (or, with no value, removes) `canvas`'s lens correction. A perspective
// correction is kept on the same spots of the photo, and the sheet it
// squares stays where it is on the plane. False if the picture is missing.
bool setCanvasLens(const cad::Document &doc, cad::ReferenceImage &canvas,
                   const std::optional<cad::ReferenceImage::Lens> &lens);

// Warps `photo` so the quad `corners` (photo pixels: top-left, top-right,
// bottom-right, bottom-left) becomes a `realWidth` x `realHeight` rectangle
// seen straight on: a true top view at an even scale. `mmPerPixel` is the
// scale of the result (at most 4096 pixels a side); `rectCentre`, if given,
// where the rectangle's middle is in it (pixels). Null if the quad is
// degenerate.
QImage correctPerspective(const QImage &photo, const std::array<cad::Vec2, 4> &corners, double realWidth,
                          double realHeight, double &mmPerPixel, cad::Vec2 *rectCentre = nullptr);

// Corrects `canvas`'s perspective from four corners of a `realWidth` x
// `realHeight` rectangle in its photo: the picture becomes a true top view at
// scale, placed so the rectangle's middle is where the canvas's middle was.
// False if the corners make no usable rectangle.
bool applyPerspective(const cad::Document &doc, cad::ReferenceImage &canvas, const std::array<cad::Vec2, 4> &corners,
                      double realWidth, double realHeight);

// Four clicked corners put in order: top-left, top-right, bottom-right,
// bottom-left (in pixel coordinates, y down).
std::array<cad::Vec2, 4> orderCorners(std::array<cad::Vec2, 4> corners);

// The part in a picture: the box of what is not its background (pixels).
cad::PixelBox partBox(const QImage &picture);

// Canvases for photos of a part from the front, the right side and the top
// (their images already in `doc`), on XZ, YZ and XY at one scale from the
// size measured along `axis` (see cad::alignViews). `out` gets one canvas per
// view, named after it; the result says the part's size, or what is wrong.
struct ViewPhoto {
    cad::ViewSide side = cad::ViewSide::Front;
    std::string imageKey;
};
cad::ViewsResult viewCanvases(const cad::Document &doc, const std::vector<ViewPhoto> &views, int axis, double mm,
                              std::vector<cad::ReferenceImage> &out);

} // namespace cadjitsu
