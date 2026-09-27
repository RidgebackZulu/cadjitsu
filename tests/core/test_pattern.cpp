// Mirror and patterns of bodies and features.
#include <doctest.h>

#include "TestModels.h"
#include "features/PatternFeature.h"

#include <cmath>

using namespace cadtest;

namespace {

void requireOk(Document &doc) {
    for(const auto &f : doc.features()) {
        INFO(f->name << ": " << doc.statusOf(f->id).message);
        REQUIRE(doc.statusOf(f->id).isOk());
    }
}

FeatureId boxBody(Document &doc, Vec2 a, Vec2 b, const char *h) {
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), a, b));
    return doc.addFeature(extrudeAll(doc, s, h));
}

double xMin(const Body &b) {
    double x0, y0, z0, x1, y1, z1;
    boundingBox(b.shape.shape()).Get(x0, y0, z0, x1, y1, z1);
    return x0;
}

} // namespace

TEST_CASE("mirror a body: kept apart, or joined where it touches") {
    for(bool join : {false, true}) {
        Document doc;
        boxBody(doc, {0, 0}, {40, 20}, "10");
        auto m = std::make_shared<PatternFeature>();
        m->kind = PatternKind::Mirror;
        m->bodies = {"b2"};
        m->plane = PlaneRef::origin(PlaneRef::Kind::YZ);
        m->join = join;
        doc.addFeature(m);
        requireOk(doc);
        const StatePtr s = doc.displayedState();
        CHECK(totalVolume(s) == doctest::Approx(16000));
        CHECK(s->bodies.size() == (join ? 1u : 2u));
        double lowest = 1e9;
        for(const auto &kv : s->bodies) lowest = std::min(lowest, xMin(*kv.second));
        CHECK(lowest == doctest::Approx(-40));
    }
}

TEST_CASE("a 3 x 2 rectangular pattern of bodies") {
    Document doc;
    boxBody(doc, {0, 0}, {10, 10}, "5");
    auto p = std::make_shared<PatternFeature>();
    p->kind = PatternKind::Rectangular;
    p->bodies = {"b2"};
    p->join = false;
    p->dir1.builtin = "x";
    p->count1 = doc.makeSlot("3");
    p->spacing1 = doc.makeSlot("15 mm");
    p->dir2.builtin = "y";
    p->count2 = doc.makeSlot("2");
    p->spacing2 = doc.makeSlot("20 mm");
    doc.addFeature(p);
    requireOk(doc);
    const StatePtr s = doc.displayedState();
    CHECK(s->bodies.size() == 6);
    CHECK(totalVolume(s) == doctest::Approx(6 * 500));
    // Leaving one copy out.
    auto q = std::static_pointer_cast<PatternFeature>(p->clone());
    q->skip = {2};
    doc.replaceFeature(q);
    CHECK(doc.displayedState()->bodies.size() == 5);
    // A count is a parameter.
    bool found = false;
    for(const ParamDef &d : p->params()) found |= d.label.find("Count") != std::string::npos;
    CHECK(found);
}

TEST_CASE("a circular pattern of a hole makes a bolt circle") {
    Document doc;
    const FeatureId s = doc.addFeature(circleSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, 30));
    doc.addFeature(extrudeAll(doc, s, "5"));
    const Body &disc = *onlyBody(doc.displayedState());
    auto h = std::make_shared<HoleFeature>();
    h->face = makeTopoRef(disc, TopoKind::Face, planarFaceWithNormal(disc, gp_Dir(0, 0, 1)));
    h->points = {{20, 0}};
    h->holeType = HoleType::Simple;
    h->extent = ExtentType::ThroughAll;
    h->diameter = doc.makeSlot("4 mm");
    const FeatureId hid = doc.addFeature(h);
    auto p = std::make_shared<PatternFeature>();
    p->kind = PatternKind::Circular;
    p->features = {hid};
    p->axis.builtin = "z";
    p->count = doc.makeSlot("6");
    p->angle = doc.makeSlot("360 deg");
    doc.addFeature(p);
    requireOk(doc);
    const StatePtr st = doc.displayedState();
    REQUIRE(st->bodies.size() == 1);
    const double disc0 = M_PI * 30 * 30 * 5, hole = M_PI * 2 * 2 * 5;
    CHECK(totalVolume(st) == doctest::Approx(disc0 - 6 * hole).epsilon(1e-6));
    // A later change to the hole's size carries to every copy.
    auto bigger = std::static_pointer_cast<HoleFeature>(doc.feature(hid)->clone());
    bigger->diameter.expr = "6 mm";
    doc.replaceFeature(bigger);
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(disc0 - 6 * M_PI * 9 * 5).epsilon(1e-6));
}

TEST_CASE("a row of joined bosses: an extrude repeated along an edge direction") {
    Document doc;
    boxBody(doc, {0, 0}, {60, 20}, "4");
    // A 6 x 6 boss on top, joined.
    auto p = std::make_shared<ConstructionPlaneFeature>();
    p->base = PlaneRef::origin(PlaneRef::Kind::XY);
    p->offset = doc.makeSlot("4 mm");
    const FeatureId pid = doc.addFeature(p);
    const FeatureId bs = doc.addFeature(rectSketch(PlaneRef::construction(pid), {4, 7}, {10, 13}));
    const FeatureId boss = doc.addFeature(extrudeAll(doc, bs, "6", BodyOperation::Join));
    auto row = std::make_shared<PatternFeature>();
    row->kind = PatternKind::Rectangular;
    row->features = {boss};
    row->dir1.builtin = "x";
    row->count1 = doc.makeSlot("5");
    row->spacing1 = doc.makeSlot("12 mm");
    doc.addFeature(row);
    requireOk(doc);
    const StatePtr s = doc.displayedState();
    REQUIRE(s->bodies.size() == 1);
    CHECK(totalVolume(s) == doctest::Approx(60 * 20 * 4 + 5 * 36 * 6));
}

TEST_CASE("an extrude that made a new body can be repeated") {
    Document doc;
    boxBody(doc, {0, 0}, {10, 10}, "5");
    auto row = std::make_shared<PatternFeature>();
    row->kind = PatternKind::Rectangular;
    row->features = {2}; // the extrude
    row->dir1.builtin = "x";
    row->count1 = doc.makeSlot("3");
    row->spacing1 = doc.makeSlot("20 mm");
    doc.addFeature(row);
    requireOk(doc);
    CHECK(doc.displayedState()->bodies.size() == 3);
    CHECK(totalVolume(doc.displayedState()) == doctest::Approx(1500));
}
