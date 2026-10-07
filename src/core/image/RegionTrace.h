#pragma once

#include "base/Vec2.h"

#include <cstdint>
#include <vector>

namespace cad {

// Plain image analysis for tracing photos (no Qt): an RGBA8 picture in, masks
// and outlines out. Pixel (x, y) counts from the top-left corner, y down; a
// pixel covers the square [x, x+1] x [y, y+1].

struct ImageView {
    const uint8_t *rgba = nullptr; // 4 bytes per pixel: r, g, b, a
    int width = 0, height = 0;
    int stride = 0;                 // bytes per row

    const uint8_t *at(int x, int y) const { return rgba + size_t(y) * size_t(stride) + size_t(x) * 4; }
};

struct Mask {
    int width = 0, height = 0;
    std::vector<uint8_t> bits;

    Mask() = default;
    Mask(int w, int h) : width(w), height(h), bits(size_t(w) * size_t(h), 0) {}
    bool at(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height && bits[size_t(y) * size_t(width) + size_t(x)];
    }
    void set(int x, int y, bool on = true) { bits[size_t(y) * size_t(width) + size_t(x)] = on ? 1 : 0; }
    size_t count() const;
};

// The pixels connected to (x, y) whose colour is within `tolerance` (0..1 of
// the largest RGB distance) of the colour around it: a part on its
// background, a hole in a part.
Mask regionAt(const ImageView &img, int x, int y, double tolerance);

// What is not background: the background colour is read from the picture's
// border; pixels further than `tolerance` from it are foreground. Specks are
// dropped: only the biggest connected piece is kept.
Mask foreground(const ImageView &img, double tolerance);

struct PixelBox {
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1; // inclusive
    bool empty() const { return x1 < x0 || y1 < y0; }
    int width() const { return x1 - x0 + 1; }
    int height() const { return y1 - y0 + 1; }
};
PixelBox boundingBox(const Mask &m);

// The boundary loops of a mask, along the pixel edges then smoothed (the
// stair steps of slanted edges are cut): the outer loops and the holes in
// them, each a closed polyline (first point not repeated). Loops enclosing
// fewer than `minArea` pixels are dropped. Outer loops come first.
struct TracedLoop {
    std::vector<Vec2> points;
    bool hole = false;
    double area = 0.0; // pixels
};
std::vector<TracedLoop> traceLoops(const Mask &m, double minArea);

} // namespace cad
