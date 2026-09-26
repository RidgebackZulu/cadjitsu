// STL export produces watertight, correctly oriented meshes whose volume
// matches the solid; the validator catches broken meshes; STEP round-trips.
#include <doctest.h>

#include "TestModels.h"
#include "features/BodyOps.h"
#include "io/StepIO.h"
#include "io/StlWriter.h"
#include "mesh/MeshValidator.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>

#include <filesystem>

using namespace cadtest;

namespace {

TopoDS_Shape filletedBox() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(40, 20, 10).Shape();
    BRepFilletAPI_MakeFillet mk(box);
    for(TopExp_Explorer ex(box, TopAbs_EDGE); ex.More(); ex.Next()) mk.Add(2.0, TopoDS::Edge(ex.Current()));
    return mk.Shape();
}

TopoDS_Shape plateWithHoles() {
    TopoDS_Shape plate = BRepPrimAPI_MakeBox(60, 40, 5).Shape();
    for(double x : {10.0, 30.0, 50.0}) {
        TopoDS_Shape hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, 20, -1), gp_Dir(0, 0, 1)), 3, 7).Shape();
        plate = BRepAlgoAPI_Cut(plate, hole).Shape();
    }
    return plate;
}

std::string outPath(const std::string &name) {
    std::filesystem::create_directories(CADLY_TEST_OUT_DIR);
    return std::string(CADLY_TEST_OUT_DIR) + "/" + name;
}

} // namespace

TEST_CASE("STL meshes are watertight at every resolution") {
    const std::vector<std::pair<std::string, TopoDS_Shape>> shapes = {
        {"box", BRepPrimAPI_MakeBox(40, 20, 10).Shape()},
        {"cylinder", BRepPrimAPI_MakeCylinder(8, 20).Shape()},
        {"sphere", BRepPrimAPI_MakeSphere(10).Shape()},
        {"filleted box", filletedBox()},
        {"plate with holes", plateWithHoles()},
    };
    for(const auto &[name, shape] : shapes) {
        for(StlResolution res : {StlResolution::Coarse, StlResolution::Medium, StlResolution::Fine}) {
            StlOptions opt;
            opt.resolution = res;
            StlExport ex;
            std::string err;
            REQUIRE_MESSAGE(buildStlMesh({shape}, opt, ex, err), name << ": " << err);
            INFO(name << " @ resolution " << int(res) << ": " << ex.report.summary());
            CHECK(ex.report.watertight);
            CHECK(ex.report.ok);
            CHECK(ex.report.shells == 1);
            CHECK(ex.report.volume > 0);
        }
    }
}

TEST_CASE("STL files round-trip through binary and ASCII") {
    StlOptions opt;
    StlExport ex;
    std::string err;
    REQUIRE(buildStlMesh({plateWithHoles()}, opt, ex, err));
    for(bool binary : {true, false}) {
        const std::string path = outPath(binary ? "plate.stl" : "plate_ascii.stl");
        REQUIRE(writeStlFile(path, ex.mesh, binary, "plate", err));
        TriMesh back;
        REQUIRE(readStlFile(path, back, err));
        CHECK(back.triangles.size() == ex.mesh.triangles.size());
        MeshReport r = validateMesh(back, ex.solidVolume, ex.solidVolume * 0.02);
        INFO(r.summary());
        CHECK(r.ok);
    }
}

TEST_CASE("merged bodies export as one watertight shell") {
    TopoDS_Shape a = BRepPrimAPI_MakeBox(20, 20, 20).Shape();
    TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 10), 20, 20, 20).Shape();
    StlOptions opt;
    StlExport ex;
    std::string err;
    REQUIRE(buildStlMesh({a, b}, opt, ex, err));
    CHECK(ex.report.ok);
    CHECK(ex.report.shells == 1);
    CHECK(ex.report.volume == doctest::Approx(8000 + 8000 - 1000).epsilon(1e-6));
}

TEST_CASE("the validator reports broken meshes") {
    StlOptions opt;
    StlExport ex;
    std::string err;
    REQUIRE(buildStlMesh({BRepPrimAPI_MakeBox(10, 10, 10).Shape()}, opt, ex, err));
    REQUIRE(ex.report.ok);

    TriMesh holed = ex.mesh;
    holed.triangles.pop_back();
    MeshReport r1 = validateMesh(holed);
    CHECK_FALSE(r1.ok);
    CHECK(r1.boundaryEdges == 3);

    TriMesh flipped = ex.mesh;
    std::swap(flipped.triangles[0][1], flipped.triangles[0][2]);
    MeshReport r2 = validateMesh(flipped);
    CHECK_FALSE(r2.ok);
    CHECK(r2.flippedEdges == 3);

    TriMesh inverted = ex.mesh;
    for(auto &t : inverted.triangles) std::swap(t[1], t[2]);
    MeshReport r3 = validateMesh(inverted);
    CHECK(r3.watertight);
    CHECK_FALSE(r3.ok); // inside-out
    CHECK(r3.volume < 0);

    TriMesh degenerate = ex.mesh;
    degenerate.triangles.push_back({0, 0, 1});
    CHECK_FALSE(validateMesh(degenerate).ok);
}

TEST_CASE("modelled parts from the document export cleanly") {
    // Box with a through hole and a filleted edge, built through features.
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {40, 20}));
    doc.addFeature(extrudeAll(doc, s, "10"));
    const FeatureId c = doc.addFeature(circleSketch(PlaneRef::origin(PlaneRef::Kind::XY), {20, 10}, 4));
    auto cut = extrudeAll(doc, c, "1", BodyOperation::Cut);
    cut->extent = ExtentType::ThroughAll;
    cut->direction = ExtrudeDirection::Symmetric;
    doc.addFeature(cut);
    const Body &b = *doc.displayedState()->bodies.begin()->second;
    auto f = std::make_shared<FilletFeature>();
    f->edges.push_back(makeTopoRef(b, TopoKind::Edge, edgeNear(b, gp_Pnt(40, 0, 5))));
    f->radius = doc.makeSlot("3");
    doc.addFeature(f);
    REQUIRE(doc.statusOf(f->id).isOk());

    std::vector<TopoDS_Shape> solids;
    for(const auto &kv : doc.displayedState()->bodies) solids.push_back(kv.second->shape.shape());
    StlOptions opt;
    StlExport ex;
    std::string err;
    REQUIRE(buildStlMesh(solids, opt, ex, err));
    INFO(ex.report.summary());
    CHECK(ex.report.ok);
    REQUIRE(writeStlFile(outPath("part.stl"), ex.mesh, true, "part", err));
}

TEST_CASE("STEP export keeps names and volumes") {
    std::vector<NamedSolid> solids(2);
    solids[0].name = "Bracket";
    solids[0].shape = BRepPrimAPI_MakeBox(40, 20, 10).Shape();
    solids[1].name = "Pin";
    solids[1].shape = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100, 0, 0), gp_Dir(0, 0, 1)), 3, 30).Shape();
    const std::string path = outPath("parts.step");
    std::string err;
    REQUIRE_MESSAGE(writeStepFile(path, solids, StepSchema::AP242, err), err);

    std::vector<NamedSolid> back;
    REQUIRE_MESSAGE(readStepFile(path, back, err), err);
    REQUIRE(back.size() == 2);
    double total = 0;
    std::vector<std::string> names;
    for(const auto &s : back) {
        total += volumeOf(s.shape);
        names.push_back(s.name);
    }
    CHECK(total == doctest::Approx(8000 + 3.14159265358979 * 9 * 30).epsilon(1e-6));
    CHECK(std::find(names.begin(), names.end(), "Bracket") != names.end());
    CHECK(std::find(names.begin(), names.end(), "Pin") != names.end());
}

TEST_CASE("a hairline sliver left by a boolean does not stop the STL export") {
    // An upright drawn a hair off the plate's back face: the join leaves a
    // strip of face far narrower than the mesher can triangulate.
    for(double gap : {1e-7, 5e-7, 2e-6, 1e-5}) {
        CAPTURE(gap);
        const TopoDS_Shape plate = BRepPrimAPI_MakeBox(60, 40, 8).Shape();
        const TopoDS_Shape upright = BRepPrimAPI_MakeBox(gp_Pnt(0, 32 + gap, 0), gp_Pnt(60, 40 + gap, 48)).Shape();
        const TopoDS_Shape bracket = BRepAlgoAPI_Fuse(plate, upright).Shape();
        // A shorter, drilled upright: the seam's two sides are split at
        // different points (T-junctions across the sliver).
        TopoDS_Shape shortUpright = BRepPrimAPI_MakeBox(gp_Pnt(10, 32 + gap, 8 - gap), gp_Pnt(50, 40 + gap, 48)).Shape();
        shortUpright = BRepAlgoAPI_Cut(shortUpright, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30, 30, 30), gp_Dir(0, 1, 0)), 5, 20).Shape()).Shape();
        TopoDS_Shape drilledPlate = BRepAlgoAPI_Cut(plate, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30, 38, -1), gp_Dir(0, 0, 1)), 1, 10).Shape()).Shape();
        const TopoDS_Shape tee = BRepAlgoAPI_Fuse(drilledPlate, shortUpright).Shape();
        for(const TopoDS_Shape &part : {bracket, tee})
        for(auto res : {cad::StlResolution::Coarse, cad::StlResolution::Fine}) {
            cad::StlOptions o;
            o.resolution = res;
            cad::StlExport out;
            std::string error;
            REQUIRE_MESSAGE(cad::buildStlMesh({part}, o, out, error), error);
            INFO(out.report.summary());
            CHECK(out.report.watertight);
            CHECK(out.report.shells == 1);
            CHECK(out.report.ok);
        }
    }
}

TEST_CASE("joining a body drawn a hair off another leaves no sliver faces") {
    // The sketch solver's error: an upright drawn onto the plate's back edge
    // can miss it by up to ~1e-5 mm.
    for(double gap : {1e-7, 5e-7, 3e-6, 2e-5, 9e-5}) {
        CAPTURE(gap);
        const NamedShape plate(BRepPrimAPI_MakeBox(60, 40, 8).Shape(), {});
        const NamedShape upright(BRepPrimAPI_MakeBox(gp_Pnt(0, 32 + gap, 8 - gap), gp_Pnt(60, 40 + gap, 48)).Shape(), {});
        const BooleanResult r = runBoolean(BoolOp::Fuse, {&plate}, {&upright}, "t");
        REQUIRE(r.ok);
        int faces = 0;
        for(TopExp_Explorer ex(r.shape.shape(), TopAbs_FACE); ex.More(); ex.Next()) {
            GProp_GProps g;
            BRepGProp::SurfaceProperties(ex.Current(), g);
            CHECK(g.Mass() > 1e-3);
            ++faces;
        }
        CHECK(faces == 8); // an L-shaped prism: 6 sides and 2 ends
        StlOptions o;
        o.resolution = StlResolution::Fine;
        StlExport out;
        std::string error;
        REQUIRE_MESSAGE(buildStlMesh({r.shape.shape()}, o, out, error), error);
        CHECK(out.report.ok);
    }
}

TEST_CASE("an upright a few micrometres past the plate's side exports watertight") {
    // The upright drawn from the plate's far corner with a typed width lands
    // micrometres past the plate's other side: a microscopic step.
    for(double ex : {-3e-5, -1e-5, -3e-6, -1e-6, 1e-6, 1e-5}) {
        for(double ey : {0.0, 1e-5}) {
            CAPTURE(ex);
            CAPTURE(ey);
            const NamedShape plate(BRepPrimAPI_MakeBox(60, 40, 10).Shape(), {});
            const NamedShape upright(BRepPrimAPI_MakeBox(gp_Pnt(ex, 32 + ey, 10), gp_Pnt(60, 40 + ey, 50)).Shape(), {});
            const BooleanResult r = runBoolean(BoolOp::Fuse, {&plate}, {&upright}, "t");
            REQUIRE(r.ok);
            for(auto res : {StlResolution::Coarse, StlResolution::Fine}) {
                StlOptions o;
                o.resolution = res;
                StlExport out;
                std::string error;
                REQUIRE_MESSAGE(buildStlMesh({r.shape.shape()}, o, out, error), error);
                INFO(out.report.summary());
                CHECK(out.report.ok);
            }
        }
    }
}
