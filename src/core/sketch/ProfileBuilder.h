#pragma once

#include "base/Vec2.h"

#include <string>
#include <vector>

namespace cad {

struct Sketch;

// A 2D curve fed to profile detection.
struct Curve2 {
    enum Type { Line, Arc, Circle } type = Line;
    int id = 0;       // sketch entity id
    Vec2 p0, p1;      // Line endpoints
    Vec2 c;           // Arc / Circle centre
    double r = 0.0;   // Arc / Circle radius
    double a0 = 0.0;  // Arc start angle (radians); the arc runs CCW to a0 + sweep
    double sweep = 0.0;
};

// One piece of a profile loop, oriented along the loop.
struct ProfileSeg {
    bool isArc = false;
    Vec2 p0, p1;          // start / end in loop direction
    Vec2 c;               // arc centre
    double r = 0.0;       // arc radius
    bool ccw = true;      // arc direction along the loop
    std::string key;      // stable segment key "c<curveId>.<k>"
    int curveId = 0;

    Vec2 midpoint() const;
};

struct ProfileLoop {
    std::vector<ProfileSeg> segs;
    double area = 0.0; // signed: CCW > 0

    // Polyline approximation (arcs subdivided), closed implicitly.
    std::vector<Vec2> polygon(double maxAngleStep = 0.035) const;
};

// A closed region of a sketch: an outer CCW loop plus CW hole loops.
struct Profile {
    std::string key;            // sorted segment keys of the outer loop
    ProfileLoop outer;
    std::vector<ProfileLoop> holes;
    double area = 0.0;          // net area
    Vec2 sample;                // a point strictly inside the region

    bool contains(Vec2 p) const;
};

struct ProfileBuildResult {
    std::vector<Profile> profiles;
    // Open chains and dangling pieces that did not close into regions.
    int danglingSegments = 0;
};

// Finds every minimal closed region formed by the curves, splitting them at
// intersections (lines, arcs and circles), like Fusion 360's profile shading.
ProfileBuildResult buildProfiles(const std::vector<Curve2> &curves);

// Extracts the non-construction curves of a sketch.
std::vector<Curve2> sketchCurves(const Sketch &sketch);

// Winding-number point-in-polygon test.
bool pointInPolygon(const std::vector<Vec2> &poly, Vec2 p);

} // namespace cad
