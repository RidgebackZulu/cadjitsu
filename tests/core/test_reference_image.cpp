// Canvases: reference pictures on planes, their calibration, and how they
// are kept in the document (undo, files).
#include <doctest.h>

#include "TestModels.h"
#include "base/Base64.h"
#include "doc/ReferenceImage.h"

#include <cmath>

using namespace cadtest;

TEST_CASE("base64 round trip, padding and bad input") {
    for(const std::string s : {std::string(), std::string("f"), std::string("fo"), std::string("foo"),
                               std::string("\\x00\\xff\\x10 binary"), std::string(1000, '\\x7f')}) {
        std::string back;
        REQUIRE(base64Decode(base64Encode(s), back));
        CHECK(back == s);
    }
    CHECK(base64Encode("foob") == "Zm9vYg==");
    std::string out;
    CHECK_FALSE(base64Decode("Zm9v*mFy", out));
}

TEST_CASE("canvas: pixels map onto the plane; calibration scales about the first point") {
    ReferenceImage r;
    r.pixelWidth = 200;
    r.pixelHeight = 100;
    r.mmPerPixel = 0.5;
    r.origin = {10, 20};
    // The centre is at the origin; +v goes down the picture, -y on the plane.
    CHECK(distance(r.toPlane({100, 50}), {10, 20}) < 1e-12);
    CHECK(distance(r.toPlane({0, 0}), {-40, 45}) < 1e-12);
    const auto c = r.planeCorners();
    CHECK(distance(c[2], {60, -5}) < 1e-12);
    r.rotation = 30;
    r.flip = true;
    const Vec2 p = r.toPlane({37, 81});
    CHECK(distance(r.toPixel(p), {37, 81}) < 1e-9);

    // Two marks 50 px apart (25 mm as placed) are really 40 mm apart.
    ReferenceImage k;
    k.pixelWidth = 400;
    k.pixelHeight = 300;
    k.mmPerPixel = 0.5;
    const Vec2 a = k.toPlane({100, 150}), b = k.toPlane({150, 150});
    std::string why;
    REQUIRE(calibrateReferenceImage(k, a, b, 40, why));
    CHECK(k.mmPerPixel == doctest::Approx(0.8));
    CHECK(distance(k.toPlane({100, 150}), a) < 1e-9); // the first point stays put
    CHECK(distance(k.toPlane({100, 150}), k.toPlane({150, 150})) == doctest::Approx(40));
    CHECK_FALSE(calibrateReferenceImage(k, a, a, 40, why));
    CHECK_FALSE(calibrateReferenceImage(k, a, b, 0, why));
}

TEST_CASE("canvas: kept in the document, undone, saved with its picture once") {
    Document doc;
    const std::string bytes = std::string("\\x89PNG fake picture ") + std::string(5000, 'x');
    const std::string key = doc.addImage(bytes);
    CHECK(doc.addImage(bytes) == key); // the same picture is kept once
    ReferenceImage r;
    r.plane = PlaneRef::origin(PlaneRef::Kind::XZ);
    r.imageKey = key;
    r.pixelWidth = 640;
    r.pixelHeight = 480;
    r.perspective = ReferenceImage::Perspective{{Vec2(10, 10), Vec2(600, 30), Vec2(620, 450), Vec2(5, 470)}, 297, 210};
    const int id = doc.addCanvas(r);
    REQUIRE(doc.canvas(id));
    CHECK(doc.canvas(id)->name == "Canvas1");
    CHECK(doc.undoLabel() == "Insert Canvas");

    // Calibrating is an undo step; undo puts it back.
    ReferenceImage c = *doc.canvas(id);
    c.mmPerPixel = 0.25;
    REQUIRE(doc.updateCanvas(c, true, "Calibrate Canvas1"));
    CHECK(doc.undoLabel() == "Calibrate Canvas1");
    REQUIRE(doc.undo());
    CHECK(doc.canvas(id)->mmPerPixel == doctest::Approx(0.1));
    // The undo snapshot holds the key, not the picture.
    CHECK(doc.undoSnapshot().dump().size() < 3000);

    // The file carries the picture once, and reads back.
    const json file = doc.toJson();
    REQUIRE(file.contains("images"));
    CHECK(file["images"].size() == 1);
    Document back;
    std::string err;
    REQUIRE(back.fromJson(file, err));
    REQUIRE(back.canvas(id));
    const ReferenceImage &b = *back.canvas(id);
    CHECK(b.plane.kind == PlaneRef::Kind::XZ);
    REQUIRE(b.perspective);
    CHECK(b.perspective->realWidth == doctest::Approx(297));
    CHECK(distance(b.perspective->corners[1], {600, 30}) < 1e-12);
    REQUIRE(back.image(b.imageKey));
    CHECK(*back.image(b.imageKey) == bytes);

    // Deleting the canvas drops the picture from the next save.
    REQUIRE(back.deleteCanvas(id));
    CHECK_FALSE(back.toJson().contains("images"));
    // Files from before canvases load unchanged.
    json old = file;
    old.erase("canvases");
    old.erase("images");
    Document plain;
    REQUIRE(plain.fromJson(old, err));
    CHECK(plain.canvases().empty());
    CHECK(std::find(Document::folderNames().begin(), Document::folderNames().end(), "canvases") !=
          Document::folderNames().end());
}
