// Threads: modelled helical threads and tap-drill bores, internal and external.
#include <doctest.h>

#include "TestModels.h"
#include "features/ThreadFeature.h"
#include "io/StlWriter.h"

#include <BRepAdaptor_Surface.hxx>

#include <cmath>

using namespace cadtest;

namespace {

// A 20 x 20 x 10 plate with a through hole of `dia` at its middle; returns the bore face.
struct Plate {
    Document doc;
    FeatureId hole = 0;
    explicit Plate(double dia) {
        const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {20, 20}));
        doc.addFeature(extrudeAll(doc, s, "10"));
        const Body &b = *onlyBody(doc.displayedState());
        auto h = std::make_shared<HoleFeature>();
        h->face = makeTopoRef(b, TopoKind::Face, planarFaceWithNormal(b, gp_Dir(0, 0, 1)));
        h->points = {{10, 10}};
        h->holeType = HoleType::Simple;
        h->extent = ExtentType::ThroughAll;
        h->diameter = doc.makeSlot(std::to_string(dia) + " mm");
        hole = doc.addFeature(h);
        REQUIRE(doc.statusOf(hole).isOk());
    }
    TopoRef bore() {
        const Body &b = *onlyBody(doc.displayedState());
        const int i = findFace(b, [](const TopoDS_Face &f) { return BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder; });
        REQUIRE(i > 0);
        return makeTopoRef(b, TopoKind::Face, i);
    }
    std::shared_ptr<ThreadFeature> thread(const std::string &size = {}) {
        auto t = std::make_shared<ThreadFeature>();
        t->faces = {bore()};
        t->size = size;
        t->clearance = doc.makeSlot("0.15 mm");
        return t;
    }
};

void requirePrintable(const TopoDS_Shape &s) {
    StlOptions opt;
    StlExport ex;
    std::string err;
    REQUIRE(buildStlMesh({s}, opt, ex, err));
    INFO(ex.report.summary());
    CHECK(ex.report.ok);
    CHECK(ex.report.shells == 1);
}

} // namespace

TEST_CASE("the thread table: sizes, tap drills and matching a hole") {
    const ThreadSpec *m6 = findThread("M6");
    REQUIRE(m6);
    CHECK(m6->pitch == doctest::Approx(1.0));
    CHECK(m6->tapDrill == doctest::Approx(5.0));
    CHECK(m6->modelByDefault());
    CHECK(!findThread("M3")->modelByDefault());
    const ThreadSpec *q = findThread("1/4-20");
    REQUIRE(q);
    CHECK(q->standard == "UNC");
    CHECK(q->major == doctest::Approx(6.35));
    CHECK(nearestThread(5.0, true)->name == "M6");
    CHECK(nearestThread(8.0, false)->name == "M8");
    CHECK(nearestThread(6.8, true)->name == "M8");      // the usual M8 tap drill
    CHECK(nearestThread(6.35, false)->name == "1/4-20 UNC"); // an inch boss is not near any metric size
    CHECK(nearestThread(5.1, true, "UNC")->name == "1/4-20 UNC");
    CHECK(nearestThread(60.0, true) == nullptr);
}

TEST_CASE("a modelled M6 thread through a 5 mm hole, and it prints") {
    Plate p(5.0);
    const double before = totalVolume(p.doc.displayedState());
    auto t = p.thread(); // size from the hole: M6
    p.doc.addFeature(t);
    INFO(p.doc.statusOf(t->id).message);
    REQUIRE(p.doc.statusOf(t->id).isOk());
    const Body &b = *onlyBody(p.doc.displayedState());
    const double v = volumeOf(b.shape.shape());
    // Between the plate with a (clearance-grown) major and minor bore.
    const double rMaj = 3.0 + 0.15, rMin = (6.0 - 1.0825318) / 2 + 0.15;
    CHECK(v < 4000 - M_PI * rMin * rMin * 10);
    CHECK(v > 4000 - M_PI * rMaj * rMaj * 10);
    CHECK(v < before);
    requirePrintable(b.shape.shape());
}

TEST_CASE("a small thread is left as a tap drill bore by default") {
    Plate p(2.2);
    auto t = p.thread("M3");
    p.doc.addFeature(t);
    REQUIRE(p.doc.statusOf(t->id).isOk());
    // The bore opened to the 2.5 mm tap drill.
    CHECK(totalVolume(p.doc.displayedState()) == doctest::Approx(4000 - M_PI * 1.25 * 1.25 * 10).epsilon(1e-6));
    // Asked for, it is modelled.
    auto m = std::static_pointer_cast<ThreadFeature>(t->clone());
    m->mode = ThreadMode::Modeled;
    p.doc.replaceFeature(m);
    REQUIRE(p.doc.statusOf(t->id).isOk());
    CHECK(totalVolume(p.doc.displayedState()) < 4000 - M_PI * 1.25 * 1.25 * 10);
}

TEST_CASE("an imperial thread and a thread only part of the way down") {
    Plate p(5.1);
    auto t = p.thread("1/4-20 UNC");
    t->length = p.doc.makeSlot("6 mm");
    p.doc.addFeature(t);
    INFO(p.doc.statusOf(t->id).message);
    REQUIRE(p.doc.statusOf(t->id).isOk());
    const Body &b = *onlyBody(p.doc.displayedState());
    // The lower 4 mm keeps the plain 5.1 mm bore: a point in the old bore just
    // above the bottom is still air, and just inside the old wall at mid-height of
    // the threaded part is cut away.
    const double v = volumeOf(b.shape.shape());
    CHECK(v < 4000 - M_PI * 2.55 * 2.55 * 10);
    CHECK(v > 4000 - M_PI * 2.55 * 2.55 * 4 - M_PI * 3.4 * 3.4 * 6);
    requirePrintable(b.shape.shape());
}

TEST_CASE("an external M8 thread on a boss") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {-10, -10}, {10, 10}));
    doc.addFeature(extrudeAll(doc, s, "4"));
    auto pl = std::make_shared<ConstructionPlaneFeature>();
    pl->base = PlaneRef::origin(PlaneRef::Kind::XY);
    pl->offset = doc.makeSlot("4 mm");
    const FeatureId pid = doc.addFeature(pl);
    const FeatureId cs = doc.addFeature(circleSketch(PlaneRef::construction(pid), {0, 0}, 4));
    doc.addFeature(extrudeAll(doc, cs, "12", BodyOperation::Join));
    const Body &b0 = *onlyBody(doc.displayedState());
    const double before = volumeOf(b0.shape.shape());
    const int boss = findFace(b0, [](const TopoDS_Face &f) { return BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder; });
    auto t = std::make_shared<ThreadFeature>();
    t->faces = {makeTopoRef(b0, TopoKind::Face, boss)};
    t->clearance = doc.makeSlot("0.15 mm");
    doc.addFeature(t);
    INFO(doc.statusOf(t->id).message);
    REQUIRE(doc.statusOf(t->id).isOk());
    const Body &b = *onlyBody(doc.displayedState());
    const double v = volumeOf(b.shape.shape());
    const double rMin = (8.0 - 1.0825318 * 1.25) / 2 - 0.15;
    CHECK(v < before);
    CHECK(v > before - M_PI * (16 - rMin * rMin) * 12);
    requirePrintable(b.shape.shape());
}


TEST_CASE("a tapped hole: the M5 tap drill with a modelled thread, or left for a tap") {
    for(ThreadMode mode : {ThreadMode::Auto, ThreadMode::TapDrill}) {
        Document doc;
        const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {20, 20}));
        doc.addFeature(extrudeAll(doc, s, "10"));
        const Body &b = *onlyBody(doc.displayedState());
        auto h = std::make_shared<HoleFeature>();
        h->face = makeTopoRef(b, TopoKind::Face, planarFaceWithNormal(b, gp_Dir(0, 0, 1)));
        h->points = {{10, 10}};
        h->holeType = HoleType::Tapped;
        h->thread = "M5";
        h->threadMode = mode;
        h->threadClearance = doc.makeSlot("0.15 mm");
        h->extent = ExtentType::ThroughAll;
        h->diameter = doc.makeSlot("9 mm"); // not used: the tap drill is
        doc.addFeature(h);
        INFO(doc.statusOf(h->id).message);
        REQUIRE(doc.statusOf(h->id).isOk());
        const double v = totalVolume(doc.displayedState());
        const double drilled = 4000 - M_PI * 2.1 * 2.1 * 10; // the 4.2 mm tap drill
        if(mode == ThreadMode::TapDrill) {
            CHECK(v == doctest::Approx(drilled).epsilon(1e-6));
        } else {
            CHECK(v < drilled);
            CHECK(v > 4000 - M_PI * 2.65 * 2.65 * 10);
            requirePrintable(onlyBody(doc.displayedState())->shape.shape());
        }
    }
}
