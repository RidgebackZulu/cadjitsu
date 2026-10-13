// Threads: modelled helical threads and tap-drill bores, internal and external.
#include <doctest.h>

#include "TestModels.h"
#include "features/ThreadFeature.h"
#include "io/StlWriter.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>

#include <chrono>
#include <cmath>
#include <thread>

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

// How many of 72 points round a circle of radius `r` about the vertical line
// through (cx, cy), at height `z`, are inside the solid.
int solidAround(const TopoDS_Shape &s, double cx, double cy, double r, double z) {
    int n = 0;
    for(int i = 0; i < 72; ++i) {
        const double a = i * 2 * M_PI / 72;
        BRepClass3d_SolidClassifier c(s, gp_Pnt(cx + r * std::cos(a), cy + r * std::sin(a), z), 1e-7);
        n += c.State() == TopAbs_IN;
    }
    return n;
}

} // namespace

TEST_CASE("the thread table: sizes, tap drills and matching a hole") {
    const ThreadSpec *m6 = findThread("M6");
    REQUIRE(m6);
    CHECK(m6->pitch == doctest::Approx(1.0));
    CHECK(m6->tapDrill == doctest::Approx(5.0));
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
    // Extra fine (UNEF) and the in-between sizes, by name with or without the series.
    const ThreadSpec *ef = findThread("1/2-28");
    REQUIRE(ef);
    CHECK(ef->name == "1/2-28 UNEF");
    CHECK(ef->major == doctest::Approx(12.7));
    CHECK(ef->pitch == doctest::Approx(25.4 / 28));
    CHECK(ef->tapDrill < ef->major);
    CHECK(ef->tapDrill > ef->minor() - 0.25); // 29/64": a full thread
    for(const char *n : {"9/16-12 UNC", "9/16-18 UNF", "7/8-9 UNC", "#12-24 UNC", "3/4-20 UNEF", "11/16-24 UNEF"})
        CHECK_MESSAGE(findThread(n), n);
    CHECK(nearestThread(25.4 * 0.4531, true, "UNEF")->name == "1/2-28 UNEF");
    // Every size: the tap drill sits between the minor and the major diameter.
    for(const ThreadSpec &s : threadTable()) {
        INFO(s.name);
        CHECK(s.tapDrill > s.minor() - 0.25);
        CHECK(s.tapDrill < s.major);
    }
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
    // Both faces are open: a 45 degree lead-in a little over the major
    // diameter at the face, and the thread running right up to it.
    for(const double face : {0.0, 10.0}) {
        const double in = face == 0 ? 1 : -1;
        INFO("face at z = " << face);
        CHECK(solidAround(b.shape.shape(), 10, 10, rMaj + 0.05, face + 0.05 * in) == 0);
        CHECK(solidAround(b.shape.shape(), 10, 10, rMaj + 0.3, face + 0.05 * in) == 72);
        // Just under the lead-in, the flanks of the first full turn.
        const int part = solidAround(b.shape.shape(), 10, 10, (rMin + rMaj) / 2, face + 1.2 * in);
        CHECK(part > 5);
        CHECK(part < 67);
    }
}

TEST_CASE("a small thread is modelled too") {
    Plate p(2.2);
    auto t = p.thread("M3");
    p.doc.addFeature(t);
    INFO(p.doc.statusOf(t->id).message);
    REQUIRE(p.doc.statusOf(t->id).isOk());
    const TopoDS_Shape &s = onlyBody(p.doc.displayedState())->shape.shape();
    // Bored to the (clearance-grown) minor diameter, the thread's grooves past it.
    const double rMin = (3.0 - 1.0825318 * 0.5) / 2 + 0.15, rMaj = 1.5 + 0.15;
    CHECK(solidAround(s, 10, 10, rMin - 0.05, 5) == 0);
    const int part = solidAround(s, 10, 10, (rMin + rMaj) / 2, 5);
    CHECK(part > 5);
    CHECK(part < 67);
    CHECK(solidAround(s, 10, 10, rMaj + 0.05, 5) == 72);
    requirePrintable(s);
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

TEST_CASE("a 1/2-28 UNEF thread modelled in a hole, and it prints") {
    Plate p(25.4 * 0.4531);
    auto t = p.thread("1/2-28 UNEF");
    p.doc.addFeature(t);
    INFO(p.doc.statusOf(t->id).message);
    REQUIRE(p.doc.statusOf(t->id).isOk());
    const Body &b = *onlyBody(p.doc.displayedState());
    requirePrintable(b.shape.shape());
}

TEST_CASE("any hole takes any thread: fitted on its own axis") {
    // 1/2-28 in holes from too small to far too big: each is filled where it
    // is too wide and bored again, so the thread comes out the same.
    const ThreadSpec &t = *findThread("1/2-28 UNEF");
    const double rMin = t.minor() / 2 + 0.15, rMaj = t.major / 2 + 0.15;
    for(const double dia : {8.0, 11.51, 12.7, 13.2, 17.0}) {
        INFO("a " << dia << " mm hole");
        Plate p(dia);
        auto th = p.thread("1/2-28 UNEF");
        p.doc.addFeature(th);
        const Status &st = p.doc.statusOf(th->id);
        INFO(st.message);
        REQUIRE(st.isOk());
        const TopoDS_Shape &s = onlyBody(p.doc.displayedState())->shape.shape();
        // Centred on the hole: the bore at the root all round, the grooves out
        // to the major, solid beyond.
        CHECK(solidAround(s, 10, 10, rMin - 0.05, 5) == 0);
        const int part = solidAround(s, 10, 10, (rMin + rMaj) / 2, 5);
        CHECK(part > 5);
        CHECK(part < 67);
        CHECK(solidAround(s, 10, 10, rMaj + 0.05, 5) == 72);
        requirePrintable(s);
    }
}

TEST_CASE("the countersink: its angle and diameter, or none") {
    const double rMaj = 3.0 + 0.15;
    {
        Plate p(5.0);
        auto th = p.thread("M6");
        th->countersinkAngle = 120;
        th->countersinkDiameter = p.doc.makeSlot("9 mm");
        p.doc.addFeature(th);
        INFO(p.doc.statusOf(th->id).message);
        REQUIRE(p.doc.statusOf(th->id).isOk());
        const TopoDS_Shape &s = onlyBody(p.doc.displayedState())->shape.shape();
        for(const double z : {0.02, 9.98}) {
            INFO("z = " << z);
            CHECK(solidAround(s, 10, 10, 4.4, z) == 0);
            CHECK(solidAround(s, 10, 10, 4.6, z) == 72);
        }
        // 120 degrees included: 0.5 mm in, the cone is 0.5 tan(60) narrower.
        const double rAt = 4.5 - 0.5 * std::tan(60 * M_PI / 180);
        CHECK(solidAround(s, 10, 10, rAt - 0.1, 9.5) == 0);
        CHECK(solidAround(s, 10, 10, rAt + 0.1, 9.5) == 72);
        requirePrintable(s);
    }
    {
        Plate p(5.0);
        auto th = p.thread("M6");
        th->countersink = false;
        p.doc.addFeature(th);
        REQUIRE(p.doc.statusOf(th->id).isOk());
        const TopoDS_Shape &s = onlyBody(p.doc.displayedState())->shape.shape();
        // A square entry: solid right up to the face just past the major.
        CHECK(solidAround(s, 10, 10, rMaj + 0.1, 9.98) == 72);
        CHECK(solidAround(s, 10, 10, rMaj + 0.1, 0.02) == 72);
    }
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
    // The tip (z = 16) is chamfered down to the root at 45 degrees, so a nut
    // starts true; the thread runs on to where the chamfer meets the crests.
    CHECK(solidAround(b.shape.shape(), 0, 0, rMin + 0.3, 15.95) == 0);
    const int part = solidAround(b.shape.shape(), 0, 0, (rMin + 3.85) / 2, 14.5);
    CHECK(part > 5);
    CHECK(part < 67);
}


TEST_CASE("a post thinner than the thread is grown to it") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {-10, -10}, {10, 10}));
    doc.addFeature(extrudeAll(doc, s, "4"));
    auto pl = std::make_shared<ConstructionPlaneFeature>();
    pl->base = PlaneRef::origin(PlaneRef::Kind::XY);
    pl->offset = doc.makeSlot("4 mm");
    const FeatureId pid = doc.addFeature(pl);
    const FeatureId cs = doc.addFeature(circleSketch(PlaneRef::construction(pid), {0, 0}, 2.5));
    doc.addFeature(extrudeAll(doc, cs, "12", BodyOperation::Join));
    const Body &b0 = *onlyBody(doc.displayedState());
    const double before = volumeOf(b0.shape.shape());
    const int post = findFace(b0, [](const TopoDS_Face &f) { return BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder; });
    auto t = std::make_shared<ThreadFeature>();
    t->faces = {makeTopoRef(b0, TopoKind::Face, post)};
    t->size = "M8";
    t->clearance = doc.makeSlot("0.15 mm");
    doc.addFeature(t);
    INFO(doc.statusOf(t->id).message);
    REQUIRE(doc.statusOf(t->id).isOk());
    const TopoDS_Shape &sh = onlyBody(doc.displayedState())->shape.shape();
    CHECK(volumeOf(sh) > before);
    // The M8's root and crests (less the clearance), on the post's axis.
    const double rMin = (8.0 - 1.0825318 * 1.25) / 2 - 0.15, rMaj = 4.0 - 0.15;
    CHECK(solidAround(sh, 0, 0, rMin - 0.05, 10) == 72);
    const int part = solidAround(sh, 0, 0, (rMin + rMaj) / 2, 10);
    CHECK(part > 5);
    CHECK(part < 67);
    CHECK(solidAround(sh, 0, 0, rMaj + 0.05, 10) == 0);
    requirePrintable(sh);
}

TEST_CASE("a tapped hole: the M5 tap drill with a modelled thread") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {20, 20}));
    doc.addFeature(extrudeAll(doc, s, "10"));
    const Body &b = *onlyBody(doc.displayedState());
    auto h = std::make_shared<HoleFeature>();
    h->face = makeTopoRef(b, TopoKind::Face, planarFaceWithNormal(b, gp_Dir(0, 0, 1)));
    h->points = {{10, 10}};
    h->holeType = HoleType::Tapped;
    h->thread = "M5";
    h->threadClearance = doc.makeSlot("0.15 mm");
    h->extent = ExtentType::ThroughAll;
    h->diameter = doc.makeSlot("9 mm"); // not used: the tap drill is
    doc.addFeature(h);
    INFO(doc.statusOf(h->id).message);
    REQUIRE(doc.statusOf(h->id).isOk());
    const double v = totalVolume(doc.displayedState());
    CHECK(v < 4000 - M_PI * 2.1 * 2.1 * 10); // less than the 4.2 mm tap drill leaves
    CHECK(v > 4000 - M_PI * 2.65 * 2.65 * 10);
    requirePrintable(onlyBody(doc.displayedState())->shape.shape());
    // The default countersink at the top: air just past the major at the face.
    CHECK(solidAround(onlyBody(doc.displayedState())->shape.shape(), 10, 10, 2.5 + 0.15 + 0.05, 9.98) == 0);
}

TEST_CASE("a long thread stops promptly when its compute is cancelled") {
    // M3 down 30 mm: some 60 turns, seconds of work. A newer request (the
    // next click) sets the flag; the thread must let go at once, so the next
    // preview is not kept waiting.
    const TopoDS_Shape block = BRepPrimAPI_MakeBox(gp_Pnt(-10, -10, 0), 20, 20, 30).Shape();
    const TopoDS_Shape bore = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp_Dir(0, 0, 1)), 1.25, 32).Shape();
    const NamedShape body(BRepAlgoAPI_Cut(block, bore).Shape(), {});
    std::atomic<bool> stop{false};
    std::thread later([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop = true;
    });
    const auto t0 = std::chrono::steady_clock::now();
    NamedShape result;
    std::string why;
    const bool ok = threadBody(body, *findThread("M3"), gp_Ax3(), 30, true, 0.15, false, 1.25, "t", result, why, true, true,
                               Countersink{}, &stop);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    later.join();
    CHECK(!ok);
    CHECK(why == "cancelled");
    CHECK(took < 1.5);
}
