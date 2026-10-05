#pragma once

#include "render/RenderMath.h"

namespace cad::rt {

// The surface of an FDM print, as a shading normal perturbation computed from
// the world position (mm) and the geometric normal, so no mesh UVs are needed.
// It models:
//  - layer beads on walls: each layer is an extruded bead with a rounded
//    (semi-elliptic) side, so walls are ridged with period = layer height and
//    dark seams between layers (side-wall Ra ~5-25 um for 0.1-0.3 mm layers);
//  - the staircase on slopes: flat treads of width h / tan(slope) between the
//    risers, facing up (or down under overhangs);
//  - top skins: lines one line-width apart, at +45 / -45 degrees on
//    alternate layers;
//  - bottom faces printed on the plate: they copy its grain (textured PEI) or
//    its gloss (smooth PEI); other downward faces sag in bridging lines;
//  - fine grain (matte fillers).
// Detail smaller than the pixel / ray footprint fades into extra roughness.
// GLSL twin: src/app/shaders/printsurface.glsl (keep the two in step).
struct SurfaceParams {
    float layerHeight = 0.2f;  // mm
    float lineWidth = 0.42f;   // mm
    float strength = 1.0f;     // layer line prominence (0 = smooth)
    float micro = 0.0f;        // fine grain
    float plateZ = 0.0f;       // z of the build plate (the first layer starts there)
    int plateKind = 0;         // 0 textured PEI, 1 smooth PEI, 2 none
    float footprint = 0.0f;    // size of a pixel / ray footprint on the surface, mm
};

struct SurfaceSample {
    V3 normal;                 // shading normal
    V3 tangent;                // along the extrusion (for anisotropic sheen)
    float cavity = 1.0f;       // darkening in the seams between beads (0..1)
    float roughnessAdd = 0.0f; // unresolved detail, as extra roughness
    float acrossAdd = 0.0f;    // unresolved bead crowns: extra roughness across the lines only
};

// Bead side profile: height (0..1) and slope d/dg at g in [0, 1] across a bead.
float beadProfile(float g, float &slope);

SurfaceSample printSurface(const V3 &p, const V3 &ng, const SurfaceParams &s);

} // namespace cad::rt
