// Smoke tests proving the toolchain: vendored libslvs solves, OCCT links and
// computes, and the core library reports its versions.
#include <doctest.h>

#include "base/Version.h"
#include "sketch/SlvsLock.h"

#include <slvs.h>

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>

#include <cmath>
#include <vector>

TEST_CASE("versions are reported") {
    CHECK(std::string(cad::version()).size() > 0);
    const std::string occt = cad::occtVersion();
    const bool supported = occt.rfind("7.", 0) == 0 || occt.rfind("8.", 0) == 0;
    CHECK(supported);
}

TEST_CASE("libslvs solves a 2D distance constraint") {
    std::lock_guard<std::mutex> lock(cad::slvsMutex());

    std::vector<Slvs_Param> params;
    std::vector<Slvs_Entity> entities;
    std::vector<Slvs_Constraint> constraints;
    std::vector<Slvs_hConstraint> failed(8);

    // Group 1: the XY workplane (fixed).
    Slvs_hGroup g = 1;
    double qw, qx, qy, qz;
    Slvs_MakeQuaternion(1, 0, 0, 0, 1, 0, &qw, &qx, &qy, &qz);
    params.push_back(Slvs_MakeParam(1, g, 0));
    params.push_back(Slvs_MakeParam(2, g, 0));
    params.push_back(Slvs_MakeParam(3, g, 0));
    entities.push_back(Slvs_MakePoint3d(101, g, 1, 2, 3));
    params.push_back(Slvs_MakeParam(4, g, qw));
    params.push_back(Slvs_MakeParam(5, g, qx));
    params.push_back(Slvs_MakeParam(6, g, qy));
    params.push_back(Slvs_MakeParam(7, g, qz));
    entities.push_back(Slvs_MakeNormal3d(102, g, 4, 5, 6, 7));
    entities.push_back(Slvs_MakeWorkplane(200, g, 101, 102));

    // Group 2: a line whose length must become 30.
    g = 2;
    params.push_back(Slvs_MakeParam(11, g, 10));
    params.push_back(Slvs_MakeParam(12, g, 20));
    entities.push_back(Slvs_MakePoint2d(301, g, 200, 11, 12));
    params.push_back(Slvs_MakeParam(13, g, 20));
    params.push_back(Slvs_MakeParam(14, g, 10));
    entities.push_back(Slvs_MakePoint2d(302, g, 200, 13, 14));
    entities.push_back(Slvs_MakeLineSegment(400, g, 200, 301, 302));
    constraints.push_back(
        Slvs_MakeConstraint(1, g, SLVS_C_PT_PT_DISTANCE, 200, 30.0, 301, 302, 0, 0));

    Slvs_System sys = {};
    sys.param = params.data();
    sys.params = (int)params.size();
    sys.entity = entities.data();
    sys.entities = (int)entities.size();
    sys.constraint = constraints.data();
    sys.constraints = (int)constraints.size();
    sys.failed = failed.data();
    sys.faileds = (int)failed.size();
    sys.calculateFaileds = 1;

    Slvs_Solve(&sys, g);

    REQUIRE(sys.result == SLVS_RESULT_OKAY);
    CHECK(sys.dof == 3);
    const double dx = params[9].val - params[7].val;
    const double dy = params[10].val - params[8].val;
    CHECK(std::hypot(dx, dy) == doctest::Approx(30.0).epsilon(1e-9));
}

TEST_CASE("OCCT builds a box with the right volume") {
    BRepPrimAPI_MakeBox mk(10.0, 20.0, 30.0);
    GProp_GProps props;
    BRepGProp::VolumeProperties(mk.Shape(), props);
    CHECK(props.Mass() == doctest::Approx(6000.0));
}
