#include "measure/MeasureBetween.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <doctest.h>

#include <cmath>

using namespace cad;

namespace {

// The planar face of `s` whose outward normal is `n`.
TopoDS_Face faceFacing(const TopoDS_Shape &s, const gp_Dir &n) {
    for(TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face f = TopoDS::Face(ex.Current());
        BRepAdaptor_Surface a(f);
        if(a.GetType() != GeomAbs_Plane) continue;
        gp_Dir d = a.Plane().Axis().Direction();
        if(f.Orientation() == TopAbs_REVERSED) d.Reverse();
        if(d.IsEqual(n, 1e-9)) return f;
    }
    return {};
}

TopoDS_Face cylinderFace(const TopoDS_Shape &s) {
    for(TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next())
        if(BRepAdaptor_Surface(TopoDS::Face(ex.Current())).GetType() == GeomAbs_Cylinder) return TopoDS::Face(ex.Current());
    return {};
}

} // namespace

TEST_CASE("measure: the gap between two boxes, with its closest points") {
    const TopoDS_Shape a = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 10, 10, 10).Shape();
    const TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(25, 0, 0), 10, 10, 10).Shape();
    const MeasureResult r = measureBetween(faceFacing(a, gp_Dir(1, 0, 0)), faceFacing(b, gp_Dir(-1, 0, 0)));
    REQUIRE(r.ok);
    CHECK(r.distance == doctest::Approx(15.0));
    CHECK(r.p1.X() == doctest::Approx(10.0));
    CHECK(r.p2.X() == doctest::Approx(25.0));
    // Parallel planes: 0 degrees.
    REQUIRE(r.angle);
    CHECK(*r.angle == doctest::Approx(0.0));
    // Whole bodies too.
    CHECK(measureBetween(a, b).distance == doctest::Approx(15.0));
}

TEST_CASE("measure: centre to centre of two holes and point to point") {
    const TopoDS_Shape h1 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(5, 5, 0), gp_Dir(0, 0, 1)), 1.5, 10).Shape();
    const TopoDS_Shape h2 = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(35, 45, 0), gp_Dir(0, 0, 1)), 2.0, 10).Shape();
    const MeasureResult r = measureBetween(cylinderFace(h1), cylinderFace(h2));
    REQUIRE(r.ok);
    REQUIRE(r.centreDistance);
    CHECK(*r.centreDistance == doctest::Approx(50.0));
    CHECK(r.distance == doctest::Approx(50.0 - 3.5));
    const TopoDS_Shape v1 = BRepBuilderAPI_MakeVertex(gp_Pnt(1, 2, 3)).Shape();
    const TopoDS_Shape v2 = BRepBuilderAPI_MakeVertex(gp_Pnt(4, 6, 3)).Shape();
    const MeasureResult pp = measureBetween(v1, v2);
    CHECK(pp.distance == doctest::Approx(5.0));
    REQUIRE(pp.centreDistance);
    CHECK(*pp.centreDistance == doctest::Approx(5.0));
    // A point to a hole's axis.
    const MeasureResult pa = measureBetween(v1, cylinderFace(h1));
    REQUIRE(pa.centreDistance);
    CHECK(*pa.centreDistance == doctest::Approx(std::hypot(4.0, 3.0)));
}

TEST_CASE("measure: angles between faces and edges") {
    const TopoDS_Shape a = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
    const MeasureResult r = measureBetween(faceFacing(a, gp_Dir(0, 0, 1)), faceFacing(a, gp_Dir(1, 0, 0)));
    REQUIRE(r.angle);
    CHECK(*r.angle * 180.0 / M_PI == doctest::Approx(90.0));
    CHECK(r.distance == doctest::Approx(0.0));
    // An edge lying in a face: 0 degrees to it.
    TopoDS_Edge edge;
    for(TopExp_Explorer ex(faceFacing(a, gp_Dir(0, 0, 1)), TopAbs_EDGE); ex.More() && edge.IsNull(); ex.Next())
        edge = TopoDS::Edge(ex.Current());
    const MeasureResult fe = measureBetween(faceFacing(a, gp_Dir(0, 0, 1)), edge);
    REQUIRE(fe.angle);
    CHECK(*fe.angle == doctest::Approx(0.0));
    const MeasureResult se = measureBetween(faceFacing(a, gp_Dir(1, 0, 0)), edge);
    REQUIRE(se.angle);
    const double deg = *se.angle * 180.0 / M_PI; // the edge may cross that face or lie in it
    CHECK((std::fabs(deg - 90.0) < 1e-9 || std::fabs(deg) < 1e-9));
}

#include "measure/Overhang.h"

TEST_CASE("overhangs: faces are sorted by how far they lean from vertical") {
    const OverhangOptions o; // 45 degrees
    auto lean = [](double deg) { // a downward face leaning `deg` from vertical
        const double r = deg * M_PI / 180.0;
        return std::pair{std::cos(r), -std::sin(r)};
    };
    auto kind = [&](double deg, bool plate = false) {
        const auto [x, z] = lean(deg);
        return classifyOverhang(x, 0, z, plate, o);
    };
    CHECK(classifyOverhang(0, 0, 1, false, o) == OverhangKind::Ok);  // a top face
    CHECK(classifyOverhang(1, 0, 0, false, o) == OverhangKind::Ok);  // a wall
    CHECK(kind(30) == OverhangKind::Ok);
    CHECK(kind(40) == OverhangKind::Near);
    CHECK(kind(50) == OverhangKind::Overhang);
    CHECK(kind(89) == OverhangKind::Bridge);
    CHECK(kind(90, true) == OverhangKind::Plate);

    // A downward triangle of area 2 over the plate, and the same on the plate.
    MeshData m;
    m.positions = {0, 0, 5, 0, 2, 5, 2, 0, 5, 0, 0, 0, 0, 2, 0, 2, 0, 0};
    m.normals = {0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1};
    m.indices = {0, 1, 2, 3, 4, 5};
    m.triangleFace = {7, 8};
    const OverhangReport r = analyzeOverhangs(m, o, true);
    CHECK(r.area[size_t(OverhangKind::Bridge)] == doctest::Approx(2.0));
    CHECK(r.area[size_t(OverhangKind::Plate)] == doctest::Approx(2.0));
    CHECK(r.faceOverhangArea.at(7) == doctest::Approx(2.0));
    CHECK(r.faceOverhangArea.count(8) == 0);
    CHECK(r.triangles[size_t(OverhangKind::Bridge)].size() == 9);
}
