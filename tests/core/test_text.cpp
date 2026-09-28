// Text as geometry: letters from the bundled fonts as faces and outlines.
#include <doctest.h>

#include "geom/OcctUtil.h"
#include "text/TextShape.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <algorithm>
#include <cmath>

using namespace cad;

namespace {

double loopArea(const std::vector<Vec2> &l) {
    double a = 0;
    for(size_t i = 0; i < l.size(); ++i) a += l[i].cross(l[(i + 1) % l.size()]);
    return a / 2;
}

double netArea(const TextShape &t) {
    double a = 0;
    for(const auto &l : t.loops) a += loopArea(l);
    return a;
}

double faceArea(const TopoDS_Shape &s) {
    GProp_GProps g;
    BRepGProp::SurfaceProperties(s, g);
    return g.Mass();
}

} // namespace

TEST_CASE("text: letters from the bundled fonts") {
    TextStyle st;
    st.size = 10;
    const auto t = buildText("HI", st);
    REQUIRE(t->ok);
    CHECK(t->warning.empty());
    // Two letters, no holes: two outer loops, counter-clockwise.
    CHECK(t->loops.size() == 2);
    for(const auto &l : t->loops) CHECK(loopArea(l) > 0);
    // Capitals stand on the baseline and are about 0.73 of the size tall.
    CHECK(t->min.y == doctest::Approx(0).epsilon(0.01));
    CHECK(t->max.y == doctest::Approx(7.29).epsilon(0.02));
    CHECK(t->min.x >= 0);
    // The outlines and the faces agree.
    CHECK(netArea(*t) == doctest::Approx(faceArea(t->faces)).epsilon(0.01));
    // Cached: the same text and style is the same object.
    CHECK(buildText("HI", st).get() == t.get());
}

TEST_CASE("text: holes, spacing, lines and mirror") {
    TextStyle st;
    st.size = 10;
    SUBCASE("an O is a ring") {
        const auto o = buildText("O", st);
        REQUIRE(o->ok);
        REQUIRE(o->loops.size() == 2);
        CHECK(((loopArea(o->loops[0]) > 0) != (loopArea(o->loops[1]) > 0)));
    }
    SUBCASE("letter spacing widens, lines stack") {
        const auto a = buildText("HH", st);
        TextStyle wide = st;
        wide.letterSpacing = 3;
        const auto b = buildText("HH", wide);
        CHECK(b->max.x - a->max.x == doctest::Approx(3).epsilon(0.001));
        const auto two = buildText("H\nH", st);
        CHECK(two->min.y == doctest::Approx(-12).epsilon(0.01)); // second baseline 1.2 x size below
    }
    SUBCASE("mirror") {
        const auto a = buildText("F", st);
        TextStyle m = st;
        m.mirror = true;
        const auto b = buildText("F", m);
        REQUIRE(b->ok);
        CHECK(b->min.x == doctest::Approx(a->min.x));
        CHECK(b->max.x == doctest::Approx(a->max.x));
        CHECK(netArea(*b) == doctest::Approx(netArea(*a)));
        // The F's arms point the other way: more of its area on the right.
        auto rightShare = [](const TextShape &t) {
            const double cx = (t.min.x + t.max.x) / 2;
            double r = 0, all = 0;
            for(const auto &l : t.loops)
                for(const Vec2 &p : l) {
                    all += 1;
                    r += p.x > cx;
                }
            return r / all;
        };
        CHECK(rightShare(*b) > rightShare(*a));
        CHECK(faceArea(b->faces) == doctest::Approx(faceArea(a->faces)).epsilon(0.01));
    }
    SUBCASE("bold is heavier, a missing font falls back") {
        TextStyle bold = st;
        bold.bold = true;
        CHECK(netArea(*buildText("H", bold)) > netArea(*buildText("H", st)) * 1.1);
        TextStyle none = st;
        none.font = "No Such Font";
        const auto t = buildText("H", none);
        REQUIRE(t->ok);
        CHECK(t->warning.find("not installed") != std::string::npos);
        CHECK(!buildText("", st)->ok);
    }
    SUBCASE("fonts are listed, bundled first") {
        const auto fonts = availableFonts();
        REQUIRE(!fonts.empty());
        CHECK(std::find(fonts.begin(), fonts.end(), "DejaVu Sans") != fonts.end());
        CHECK(fonts.front().rfind("DejaVu", 0) == 0);
    }
}

// --- sketch text ---------------------------------------------------------------------

#include "TestModels.h"
#include "io/StlWriter.h"
#include "sketch/SketchText.h"

namespace {

using cadtest::extrudeAll;
using cadtest::totalVolume;

int letterRegions(const std::vector<Profile> &ps) {
    int n = 0;
    for(const auto &p : ps) n += textOfProfile(p) != 0;
    return n;
}

} // namespace

TEST_CASE("sketch text: letters are regions") {
    Sketch s;
    const int t = s.addText(Vec2{10, 5}, "HI O");
    s.find(t)->size = 10;
    auto ps = sketchProfiles(s);
    // H, I and O (a ring: one region with a hole).
    CHECK(letterRegions(ps) == 3);
    int ringHoles = 0;
    for(const auto &p : ps) ringHoles += int(p.holes.size());
    CHECK(ringHoles == 1);
    for(const auto &p : ps) {
        CHECK(p.contains(p.sample));
        CHECK(p.sample.x > 10);
    }
    // Moving the origin moves the letters.
    Vec2 before = ps.front().sample;
    s.find(s.find(t)->a)->x += 7;
    CHECK(sketchProfiles(s).front().sample.x == doctest::Approx(before.x + 7));
    // Turned by its angle.
    s.find(t)->angle = 90;
    for(const auto &p : sketchProfiles(s)) CHECK(p.sample.y > 5); // now it reads upwards
    // Construction text makes no regions.
    s.find(t)->construction = true;
    CHECK(sketchProfiles(s).empty());
}

TEST_CASE("sketch text: inside a plate, and extruded") {
    Document doc;
    auto sk = cadtest::rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {60, 20});
    const int t = sk->sketch.addText(Vec2{5, 5}, "O");
    sk->sketch.find(t)->size = 10;
    const FeatureId sid = doc.addFeature(sk);
    const auto st = doc.stateAt(1);
    const auto &profiles = st->sketches.at(sid)->profiles;
    // The plate with the O cut out, the O (a ring), and the inside of the O.
    REQUIRE(profiles.size() == 3);
    double plate = 0, letters = 0;
    for(const auto &p : profiles) {
        if(textOfProfile(p)) letters += std::fabs(p.area);
        else if(p.key.find(".in") == std::string::npos) plate = std::fabs(p.area);
    }
    CHECK(plate > 1100);
    CHECK(plate < 1200 - letters + 1e-6);

    // Only the letters, extruded 2 mm: one solid per letter piece, watertight.
    auto e = std::make_shared<ExtrudeFeature>();
    for(const auto &p : profiles)
        if(textOfProfile(p)) e->profiles.push_back({sid, p.key, p.sample});
    e->distance = doc.makeSlot("2 mm");
    doc.addFeature(e);
    const auto after = doc.stateAt(2);
    REQUIRE(doc.statusOf(e->id).isOk());
    CHECK(totalVolume(after) == doctest::Approx(letters * 2).epsilon(0.005));
    std::vector<TopoDS_Shape> shapes;
    for(const auto &kv : after->bodies) shapes.push_back(kv.second->shape.shape());
    StlExport ex;
    std::string err;
    REQUIRE(buildStlMesh(shapes, StlOptions(), ex, err));
    CHECK(ex.report.watertight);
}

TEST_CASE("sketch text: saved and read back") {
    Sketch s;
    const int t = s.addText(Vec2{1, 2}, "Ab\nc");
    SkEntity &e = *s.find(t);
    e.size = 7;
    e.angle = 30;
    e.bold = true;
    e.mirror = true;
    e.font = "DejaVu Serif";
    const Sketch back = Sketch::fromJson(s.toJson());
    const SkEntity *b = back.find(t);
    REQUIRE(b);
    CHECK(b->isText());
    CHECK(b->text == "Ab\nc");
    CHECK(b->size == 7);
    CHECK(b->angle == 30);
    CHECK(b->bold);
    CHECK(b->mirror);
    CHECK(b->font == "DejaVu Serif");
    CHECK(sketchProfiles(back).size() == sketchProfiles(s).size());
    // Deleting the text takes its origin point too.
    Sketch d = s;
    d.removeEntity(t);
    CHECK(d.entities.empty());
}
