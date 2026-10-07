// Lens distortion: the radial model, plumb-line calibration from clicked
// straight edges, and undistorting a picture.
#include <doctest.h>

#include "TestModels.h"
#include "doc/ReferenceImage.h"
#include "image/LensModel.h"

#include <cmath>

using namespace cadtest;

namespace {

constexpr int W = 1600, H = 1200;

// Points along straight edges of the scene, as a lens `l` shows them.
std::vector<std::vector<Vec2>> seenEdges(const LensDistortion &l, int perLine) {
    const std::vector<std::pair<Vec2, Vec2>> edges = {
        {{100, 120}, {1500, 140}}, {{90, 1080}, {1510, 1060}}, {{130, 100}, {110, 1100}}, {{1470, 110}, {1490, 1090}}};
    std::vector<std::vector<Vec2>> out;
    for(const auto &[a, b] : edges) {
        std::vector<Vec2> line;
        for(int i = 0; i < perLine; ++i) line.push_back(distortPixel(l, a + (b - a) * (double(i) / (perLine - 1)), W, H));
        out.push_back(line);
    }
    return out;
}

} // namespace

TEST_CASE("lens: distorting and undistorting a pixel are inverses; the centre stays") {
    const LensDistortion barrel{-0.18, 0.03};
    for(const Vec2 p : {Vec2(0, 0), Vec2(800, 600), Vec2(1600, 1200), Vec2(1234, 77)}) {
        const Vec2 seen = distortPixel(barrel, p, W, H);
        CHECK(distance(undistortPixel(barrel, seen, W, H), p) < 1e-6);
    }
    CHECK(distance(distortPixel(barrel, {800, 600}, W, H), {800, 600}) < 1e-12);
    // Barrel: the corners are seen pulled in towards the middle.
    CHECK(distance(distortPixel(barrel, {0, 0}, W, H), {800, 600}) < 1000.0 * 0.9);
}

TEST_CASE("lens: plumb lines find barrel and pincushion distortion") {
    for(const LensDistortion truth : {LensDistortion{-0.15, 0.0}, LensDistortion{0.08, 0.0}, LensDistortion{-0.2, 0.05}}) {
        const auto lines = seenEdges(truth, 7);
        CHECK(lineStraightness({}, lines, W, H) > 5.0); // visibly bent
        const auto found = estimateLens(lines, W, H);
        REQUIRE(found);
        // Straight again, to well under a pixel.
        CHECK(lineStraightness(*found, lines, W, H) < 0.5);
        CHECK(found->k1 == doctest::Approx(truth.k1).epsilon(0.1).scale(1.0));
    }
    // Straight lines: no correction.
    const auto straight = seenEdges({}, 5);
    const auto none = estimateLens(straight, W, H);
    REQUIRE(none);
    CHECK(std::fabs(none->k1) < 1e-3);
    // Two points a line tell nothing.
    CHECK_FALSE(estimateLens({{{0, 0}, {10, 10}}}, W, H));
}

TEST_CASE("lens: undistorting a picture straightens a bent edge") {
    const LensDistortion barrel{-0.2, 0.0};
    const int w = 200, h = 150;
    // A dark band, straight in reality, along y = 20 (near the top edge).
    std::vector<std::uint8_t> photo(size_t(w) * h * 4, 255);
    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x) {
            const Vec2 real = undistortPixel(barrel, {x + 0.5, y + 0.5}, w, h);
            if(std::fabs(real.y - 20) < 3)
                for(int c = 0; c < 3; ++c) photo[(size_t(y) * w + x) * 4 + size_t(c)] = 0;
        }
    const ImageView view{photo.data(), w, h, w * 4};
    const auto out = undistortImage(view, barrel);
    auto darkAt = [&](const std::vector<std::uint8_t> &img, int x, int y) { return img[(size_t(y) * w + x) * 4] < 128; };
    // In the photo the band bows: at the sides it is lower than in the middle.
    CHECK(darkAt(photo, 100, 20));
    CHECK_FALSE(darkAt(photo, 5, 20));
    // Corrected: it runs straight along y = 20.
    for(int x : {10, 50, 100, 150, 190}) CHECK(darkAt(out, x, 20));
}

TEST_CASE("lens: kept with the canvas in its file") {
    ReferenceImage r;
    r.imageKey = "k";
    r.lens = ReferenceImage::Lens{{-0.1, 0.02}, {{{1, 2}, {3, 4}, {5, 6}}}};
    const ReferenceImage back = ReferenceImage::fromJson(r.toJson());
    REQUIRE(back.lens);
    CHECK(back.lens->distortion.k1 == doctest::Approx(-0.1));
    CHECK(back.lens->distortion.k2 == doctest::Approx(0.02));
    REQUIRE(back.lens->lines.size() == 1);
    CHECK(distance(back.lens->lines[0][2], {5, 6}) < 1e-12);
    CHECK_FALSE(ReferenceImage::fromJson(ReferenceImage().toJson()).lens);
}
