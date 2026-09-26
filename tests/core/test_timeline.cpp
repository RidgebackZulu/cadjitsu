// Timeline behaviour: marker, suppression, insertion, undo/redo, caching,
// parameter references and document serialization.
#include <doctest.h>

#include "TestModels.h"

#include <cstdio>
#include <filesystem>

using namespace cadtest;

namespace {

constexpr double PI = 3.14159265358979323846;

// sketch(1) -> extrude(2, 10 mm) -> fillet(3, 2 mm on one vertical edge)
struct Model {
    Document doc;
    FeatureId sketch, extrude, fillet;
    Model() {
        sketch = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {40, 20}));
        extrude = doc.addFeature(extrudeAll(doc, sketch, "10 mm"));
        const Body &b = *doc.displayedState()->bodies.begin()->second;
        auto f = std::make_shared<FilletFeature>();
        f->edges.push_back(makeTopoRef(b, TopoKind::Edge, edgeNear(b, gp_Pnt(40, 0, 5))));
        f->radius = doc.makeSlot("2 mm");
        fillet = doc.addFeature(f);
    }
    double volume() { return totalVolume(doc.displayedState()); }
};

const double kFilleted = 8000 - (4 - PI) * 10;

} // namespace

TEST_CASE("states along the timeline and the marker") {
    Model m;
    CHECK(m.doc.marker() == 3);
    CHECK(m.doc.stateAt(0)->bodies.empty());
    CHECK(m.doc.stateAt(1)->bodies.empty());
    CHECK(m.doc.stateAt(1)->sketches.size() == 1);
    CHECK(totalVolume(m.doc.stateAt(2)) == doctest::Approx(8000));
    CHECK(totalVolume(m.doc.stateAt(3)) == doctest::Approx(kFilleted));

    m.doc.setMarker(2);
    CHECK(m.volume() == doctest::Approx(8000));
    m.doc.setMarker(1);
    CHECK(m.doc.displayedState()->bodies.empty());
    m.doc.setMarker(3);
    CHECK(m.volume() == doctest::Approx(kFilleted));
}

TEST_CASE("scrubbing and un-suppressing hit the cache") {
    Model m;
    m.doc.stateAt(3);
    const size_t computed = m.doc.computeCount();
    for(int i = 0; i <= 3; ++i) m.doc.setMarker(i, false);
    m.doc.stateAt(3);
    CHECK(m.doc.computeCount() == computed);

    m.doc.setSuppressed(m.fillet, true);
    CHECK(m.volume() == doctest::Approx(8000));
    m.doc.setSuppressed(m.fillet, false);
    CHECK(m.volume() == doctest::Approx(kFilleted));
    CHECK(m.doc.computeCount() == computed);
}

TEST_CASE("suppressing an upstream feature propagates") {
    Model m;
    m.doc.setSuppressed(m.extrude, true);
    CHECK(m.doc.displayedState()->bodies.empty());
    CHECK(m.doc.statusOf(m.fillet).isError()); // its edge is gone
    m.doc.setSuppressed(m.extrude, false);
    CHECK(m.doc.statusOf(m.fillet).isOk());
    CHECK(m.volume() == doctest::Approx(kFilleted));
}

TEST_CASE("edit, undo and redo") {
    Model m;
    m.doc.stateAt(3);
    const size_t before = m.doc.computeCount();
    auto ex = std::static_pointer_cast<ExtrudeFeature>(m.doc.feature(m.extrude)->clone());
    ex->distance.expr = "20 mm";
    m.doc.replaceFeature(ex);
    CHECK(m.volume() == doctest::Approx(16000 - (4 - PI) * 20));
    CHECK(m.doc.computeCount() == before + 2); // extrude + fillet recomputed

    REQUIRE(m.doc.canUndo());
    CHECK(m.doc.undoLabel() == "Edit Extrude1");
    m.doc.undo();
    CHECK(m.volume() == doctest::Approx(kFilleted));
    CHECK(m.doc.computeCount() == before + 2); // undo is a cache hit
    m.doc.redo();
    CHECK(m.volume() == doctest::Approx(16000 - (4 - PI) * 20));
    CHECK(m.doc.computeCount() == before + 2);

    // Undo everything back to an empty document.
    while(m.doc.canUndo()) m.doc.undo();
    CHECK(m.doc.features().empty());
    CHECK(m.doc.displayedState()->bodies.empty());
    while(m.doc.canRedo()) m.doc.redo();
    CHECK(m.doc.features().size() == 3);
}

TEST_CASE("features are inserted at the marker; delete adjusts it") {
    Model m;
    m.doc.setMarker(2);
    const Body &b = *m.doc.displayedState()->bodies.begin()->second;
    auto ch = std::make_shared<ChamferFeature>();
    // The back top edge: it does not touch the filleted corner.
    ch->edges.push_back(makeTopoRef(b, TopoKind::Edge, edgeNear(b, gp_Pnt(20, 20, 10))));
    ch->distance = m.doc.makeSlot("1 mm");
    const FeatureId cid = m.doc.addFeature(ch);
    CHECK(m.doc.indexOf(cid) == 2);
    CHECK(m.doc.marker() == 3);
    CHECK(m.doc.features()[3]->id == m.fillet);
    CHECK(m.doc.feature(cid)->name == "Chamfer1");
    m.doc.setMarker(4);
    CHECK(m.doc.statusOf(m.fillet).isOk());
    CHECK(m.volume() == doctest::Approx(kFilleted - 20).epsilon(1e-6));

    m.doc.deleteFeature(cid);
    CHECK(m.doc.marker() == 3);
    CHECK(m.volume() == doctest::Approx(kFilleted));
}

TEST_CASE("parameters reference each other across features") {
    Model m;
    const std::string dist = std::static_pointer_cast<const ExtrudeFeature>(m.doc.feature(m.extrude))->distance.name;
    auto f = std::static_pointer_cast<FilletFeature>(m.doc.feature(m.fillet)->clone());
    f->radius.expr = dist + " / 5";
    m.doc.replaceFeature(f);
    CHECK(m.doc.params().find(f->radius.name)->value == doctest::Approx(2));
    CHECK(m.volume() == doctest::Approx(kFilleted));

    auto ex = std::static_pointer_cast<ExtrudeFeature>(m.doc.feature(m.extrude)->clone());
    ex->distance.expr = "15";
    m.doc.replaceFeature(ex);
    CHECK(m.doc.params().find(f->radius.name)->value == doctest::Approx(3));
    CHECK(m.volume() == doctest::Approx(40 * 20 * 15 - (9 - 9 * PI / 4) * 15).epsilon(1e-6));

    // A broken expression is an error on that feature only.
    auto bad = std::static_pointer_cast<FilletFeature>(m.doc.feature(m.fillet)->clone());
    bad->radius.expr = "nonsense +";
    m.doc.replaceFeature(bad);
    CHECK(m.doc.statusOf(m.fillet).isError());
    CHECK(m.doc.statusOf(m.extrude).isOk());
}

TEST_CASE("renaming does not recompute") {
    Model m;
    m.doc.stateAt(3);
    const size_t before = m.doc.computeCount();
    m.doc.renameFeature(m.extrude, "Base");
    m.doc.renameBody("b2", "Bracket");
    CHECK(m.doc.feature(m.extrude)->name == "Base");
    CHECK(m.doc.bodyName(*m.doc.displayedState()->bodies.at("b2")) == "Bracket");
    m.doc.stateAt(3);
    CHECK(m.doc.computeCount() == before);
}

TEST_CASE("document JSON round trip is byte-identical and reproduces the model") {
    Model m;
    m.doc.setMarker(2);
    m.doc.renameBody("b2", "Bracket");
    m.doc.setBodyVisible("b2", false);
    const std::string a = m.doc.toJson().dump(2);

    Document d2;
    std::string err;
    REQUIRE(d2.fromJson(json::parse(a), err));
    CHECK(d2.toJson().dump(2) == a);
    CHECK(d2.marker() == 2);
    CHECK_FALSE(d2.bodyVisible("b2"));
    d2.setMarker(3);
    CHECK(totalVolume(d2.displayedState()) == doctest::Approx(kFilleted));

    // Through a file.
    namespace fs = std::filesystem;
    fs::create_directories(CADLY_TEST_OUT_DIR);
    const std::string path = std::string(CADLY_TEST_OUT_DIR) + "/roundtrip.cadly";
    REQUIRE(m.doc.save(path, err));
    Document d3;
    REQUIRE(d3.load(path, err));
    CHECK(d3.toJson().dump(2) == a);

    Document bad;
    CHECK_FALSE(bad.fromJson(json{{"format", "other"}}, err));
}

TEST_CASE("new parameter names are never reused") {
    Model m;
    const std::string n1 = m.doc.allocateParamName();
    const std::string n2 = m.doc.allocateParamName();
    CHECK(n1 != n2);
    for(const auto &f : m.doc.features())
        for(const auto &p : f->params()) {
            CHECK(p.name != n1);
            CHECK(p.name != n2);
        }
}

TEST_CASE("sketch visibility overrides are saved and undone") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {10, 10}));
    CHECK_FALSE(doc.sketchVisibility(s).has_value());
    doc.pushUndo("Hide Sketch");
    doc.setSketchVisible(s, false);
    REQUIRE(doc.sketchVisibility(s).has_value());
    CHECK_FALSE(*doc.sketchVisibility(s));
    Document copy;
    std::string err;
    REQUIRE(copy.fromJson(doc.toJson(), err));
    REQUIRE(copy.sketchVisibility(s).has_value());
    CHECK_FALSE(*copy.sketchVisibility(s));
    doc.undo();
    CHECK_FALSE(doc.sketchVisibility(s).has_value());
}

TEST_CASE("a marker drag is one undo step") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {10, 10}));
    doc.addFeature(extrudeAll(doc, s, "5"));
    const size_t steps = 2;
    const json before = doc.undoSnapshot();
    for(int m : {1, 0, 1}) doc.setMarker(m, false);
    doc.pushUndoSnapshot("Move History Marker", before);
    CHECK(doc.marker() == 1);
    CHECK(doc.undoLabel() == "Move History Marker");
    REQUIRE(doc.undo());
    CHECK(doc.marker() == 2);
    CHECK(doc.features().size() == steps);
}

TEST_CASE("known states come from the shared cache and never compute") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {10, 10}));
    doc.addFeature(extrudeAll(doc, s, "5"));
    CHECK(doc.knownStateAt(0) != nullptr); // the empty model
    // A second document sharing nothing: nothing is known until it is computed.
    Document other;
    std::string err;
    REQUIRE(other.fromJson(doc.toJson(), err));
    const size_t before = other.computeCount();
    CHECK(other.knownStateAt(2) == nullptr);
    CHECK(other.computeCount() == before);
    // A background evaluation sharing the cache makes it known.
    TimelineEvaluation eval;
    const auto params = buildParamTable(other.features());
    evaluateTimeline(other.features(), *params, 2, eval, *other.sharedCache());
    const StatePtr known = other.knownStateAt(2);
    REQUIRE(known != nullptr);
    CHECK(other.computeCount() == before);
    CHECK(known == eval.states[1]);
    CHECK(std::fabs(totalVolume(known) - 500.0) < 1e-6);
    // Suppressed features pass the state through without a cache entry.
    other.setSuppressed(other.features()[1]->id, true);
    REQUIRE(other.knownStateAt(2) != nullptr);
    CHECK(other.knownStateAt(2)->bodies.empty());
}
