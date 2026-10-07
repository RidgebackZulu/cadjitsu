// Two / three views of a part set up at one scale on XZ, YZ and XY.
#include <doctest.h>

#include "TestModels.h"
#include "doc/Document.h"
#include "image/ViewAlign.h"

using namespace cadtest;

namespace {

PixelBox box(int x0, int y0, int w, int h) { return {x0, y0, x0 + w - 1, y0 + h - 1}; }

// Where a picture pixel lands on its plane.
Vec2 onPlane(const ViewPlacement &p, const ViewInput &v, double px, double py) {
    return p.origin + Vec2((px - v.width / 2.0) * p.mmPerPixel, -(py - v.height / 2.0) * p.mmPerPixel);
}

} // namespace

TEST_CASE("views: a 60 x 40 x 30 block from three photos at different zooms") {
    // Front: X 60 across, Z 30 up, at 5 px/mm; side: Y 40, Z 30 at 4 px/mm;
    // top: X 60, Y 40 at 3 px/mm. Each off-centre in its picture.
    const std::vector<ViewInput> in = {{ViewSide::Front, 800, 600, box(100, 300, 300, 150)},
                                       {ViewSide::Side, 640, 480, box(300, 100, 160, 120)},
                                       {ViewSide::Top, 500, 500, box(50, 60, 180, 120)}};
    const ViewsResult r = alignViews(in, 0, 60.0); // the width, measured: 60 mm
    REQUIRE(r.ok);
    CHECK(r.warnings.empty());
    CHECK(r.size[0] == doctest::Approx(60));
    CHECK(r.size[1] == doctest::Approx(40));
    CHECK(r.size[2] == doctest::Approx(30));
    REQUIRE(r.views.size() == 3);
    CHECK(r.views[0].mmPerPixel == doctest::Approx(0.2));
    CHECK(r.views[1].mmPerPixel == doctest::Approx(0.25));
    CHECK(r.views[2].mmPerPixel == doctest::Approx(1.0 / 3));
    // Front: the box from x -30..30, z 0..30.
    const Vec2 fBL = onPlane(r.views[0], in[0], 100, 450), fTR = onPlane(r.views[0], in[0], 400, 300);
    CHECK(distance(fBL, {-30, 0}) < 1e-9);
    CHECK(distance(fTR, {30, 30}) < 1e-9);
    // Side: y -20..20, z 0..30.
    CHECK(distance(onPlane(r.views[1], in[1], 300, 220), {-20, 0}) < 1e-9);
    // Top: centred on the origin.
    CHECK(distance(onPlane(r.views[2], in[2], 230, 60), {30, 20}) < 1e-9);
    CHECK(std::string(viewPlaneName(ViewSide::Side)) == "yz");
}

TEST_CASE("views: the height known; two views; disagreement and errors") {
    // Front and side only, the height measured.
    const std::vector<ViewInput> two = {{ViewSide::Front, 400, 400, box(0, 0, 200, 100)},
                                        {ViewSide::Side, 400, 400, box(0, 0, 50, 100)}};
    const ViewsResult r = alignViews(two, 2, 20.0);
    REQUIRE(r.ok);
    CHECK(r.size[0] == doctest::Approx(40));
    CHECK(r.size[1] == doctest::Approx(10));
    // A top view whose proportions disagree: warned, still placed.
    const std::vector<ViewInput> odd = {{ViewSide::Front, 400, 400, box(0, 0, 200, 100)},
                                        {ViewSide::Top, 400, 400, box(0, 0, 200, 150)},
                                        {ViewSide::Side, 400, 400, box(0, 0, 50, 100)}};
    const ViewsResult w = alignViews(odd, 0, 40.0);
    REQUIRE(w.ok);
    CHECK_FALSE(w.warnings.empty());
    // The top view alone cannot tell the height.
    CHECK_FALSE(alignViews({{ViewSide::Top, 100, 100, box(0, 0, 50, 50)}}, 2, 10.0).ok);
    CHECK_FALSE(alignViews({{ViewSide::Top, 100, 100, PixelBox{}}}, 0, 10.0).ok);
    CHECK_FALSE(alignViews(two, 0, 0.0).ok);
    CHECK_FALSE(alignViews({two[0], two[0]}, 0, 10.0).ok);
}

TEST_CASE("views: several canvases added as one undo step") {
    Document doc;
    std::vector<ReferenceImage> cs(3);
    for(auto &c : cs) c.imageKey = "k";
    const auto ids = doc.addCanvases(cs, "Insert Views");
    CHECK(ids.size() == 3);
    CHECK(doc.canvases().size() == 3);
    CHECK(doc.undoLabel() == "Insert Views");
    doc.undo();
    CHECK(doc.canvases().empty());
}
