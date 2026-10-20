// Remove: bodies deleted from a point of the timeline on.
#include <doctest.h>

#include "TestModels.h"
#include "features/FilletFeature.h"
#include "features/RemoveFeature.h"

using namespace cadtest;

namespace {

struct TwoBoxes {
    Document doc;
    BodyId a, b;
    TwoBoxes() {
        const FeatureId sa = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {10, 10}));
        doc.addFeature(extrudeAll(doc, sa, "10"));
        a = doc.displayedState()->bodies.begin()->first;
        const FeatureId sb = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {30, 0}, {50, 20}));
        doc.addFeature(extrudeAll(doc, sb, "5"));
        for(const auto &kv : doc.displayedState()->bodies)
            if(kv.first != a) b = kv.first;
        REQUIRE(!b.empty());
    }
};

} // namespace

TEST_CASE("remove a body, build on what is left, and bring it back") {
    TwoBoxes m;
    auto rm = std::make_shared<RemoveFeature>();
    rm->bodies = {m.b};
    const FeatureId id = m.doc.addFeature(rm);
    INFO(m.doc.statusOf(id).message);
    REQUIRE(m.doc.statusOf(id).isOk());
    REQUIRE(m.doc.displayedState()->bodies.size() == 1);
    CHECK(m.doc.displayedState()->body(m.a));
    CHECK(!m.doc.displayedState()->body(m.b));
    CHECK(totalVolume(m.doc.displayedState()) == doctest::Approx(1000).epsilon(1e-9));

    // A fillet on the box that is left, after the Remove step.
    auto fil = std::make_shared<FilletFeature>();
    fil->edges.push_back(makeTopoRef(*m.doc.displayedState()->body(m.a), TopoKind::Edge, 1));
    fil->radius = m.doc.makeSlot("1 mm");
    const FeatureId fid = m.doc.addFeature(fil);
    INFO(m.doc.statusOf(fid).message);
    CHECK(m.doc.statusOf(fid).isOk());
    CHECK(totalVolume(m.doc.displayedState()) < 1000);

    // Saved and read back.
    RemoveFeature back;
    back.dataFromJson(rm->dataToJson());
    CHECK((back.bodies == rm->bodies));
    CHECK((Feature::create(FeatureType::Remove)->type() == FeatureType::Remove));

    // Suppressing the step brings the body back; so does deleting it.
    m.doc.setSuppressed(id, true);
    CHECK(m.doc.displayedState()->bodies.size() == 2);
    m.doc.setSuppressed(id, false);
    CHECK(m.doc.displayedState()->bodies.size() == 1);
    REQUIRE(m.doc.deleteFeature(id));
    CHECK(m.doc.displayedState()->bodies.size() == 2);
    CHECK(m.doc.statusOf(fid).isOk());
}

TEST_CASE("remove: a body already gone is a warning, none at all an error") {
    TwoBoxes m;
    auto rm = std::make_shared<RemoveFeature>();
    rm->bodies = {m.b, "b999"};
    const FeatureId id = m.doc.addFeature(rm);
    CHECK((m.doc.statusOf(id).severity == Severity::Warning));
    CHECK(m.doc.displayedState()->bodies.size() == 1);
    auto none = std::make_shared<RemoveFeature>();
    none->bodies = {"b999"};
    const FeatureId nid = m.doc.addFeature(none);
    CHECK(m.doc.statusOf(nid).isError());
    auto empty = std::make_shared<RemoveFeature>();
    CHECK(m.doc.statusOf(m.doc.addFeature(empty)).isError());
}
