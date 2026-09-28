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
