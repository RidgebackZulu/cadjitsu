#pragma once

#include "render/Materials.h"
#include "render/RenderMath.h"

#include <vector>

namespace cad::rt {

// The Snapmaker U1's build plate, which rendered models stand on: a
// double-sided PEI spring-steel sheet, about 276 x 293 x 1 mm with rounded
// corners and a front grab tab, giving a 270 x 270 mm print area, held by
// magnets on a black heated bed. The model is never moved: the plate is laid
// under it (centred, or with the print area's front-left corner at the
// origin) with its top at the model's lowest point.
struct PlateLayout {
    bool present = false;
    BuildPlateKind kind = BuildPlateKind::TexturedPEI;
    float top = 0.0f;              // z of the print surface
    float cx = 0.0f, cy = 0.0f;    // centre of the print area
    float area = 270.0f;           // print area (square), mm
    float sheetW = 276.0f, sheetD = 293.0f, sheetT = 1.0f; // sheet, mm
    float sheetCy = 0.0f;          // sheet centre y (the sheet runs further forward than the area)
    float corner = 6.0f;           // sheet corner radius
    float tabW = 70.0f, tabD = 9.0f; // front grab tab
    float bedW = 300.0f, bedD = 312.0f, bedT = 8.0f; // heated bed below
    float bedCy = 0.0f;
};

PlateLayout plateLayout(const V3 &bboxMin, const V3 &bboxMax, const RenderSettings &settings);

// The plate as triangles: part 0 = PEI top, 1 = steel edges and underside,
// 2 = heated bed. Normals per vertex (flat).
struct PlateMesh {
    std::vector<V3> positions, normals;
    std::vector<uint32_t> indices;
    std::vector<int> part;          // per triangle
};
PlateMesh plateMesh(const PlateLayout &layout);

// How a point of the plate looks (linear colours).
struct PlateShade {
    V3 albedo;
    V3 normal;
    float roughness = 0.5f;
    float metalness = 0.0f;
};
PlateShade plateShade(const PlateLayout &layout, int part, const V3 &p, const V3 &ng);

} // namespace cad::rt
