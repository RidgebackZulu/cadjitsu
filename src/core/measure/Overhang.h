#pragma once

#include "mesh/MeshData.h"

#include <array>
#include <map>
#include <vector>

namespace cad {

// How printable a surface is when printed upwards (+Z), layer on layer.
enum class OverhangKind { Ok, Near, Overhang, Bridge, Plate, Count };

struct OverhangOptions {
    double threshold = 45.0; // degrees from vertical a downward face may lean before it needs support
    double nearBand = 10.0;  // degrees short of the threshold that count as "near"
    double flatBand = 5.0;   // degrees from facing straight down that count as a bridge
    double plateZ = 0.0;     // the build plate; flat faces resting on it need nothing
};

// A surface with outward normal (nx, ny, nz); `onPlate` if it rests on the build plate.
OverhangKind classifyOverhang(double nx, double ny, double nz, bool onPlate, const OverhangOptions &o);

struct OverhangReport {
    std::array<double, size_t(OverhangKind::Count)> area{}; // mm2 per kind
    std::map<int, double> faceOverhangArea; // B-rep face (1-based) -> area needing support (Overhang + Bridge)
    // Triangles per kind (xyz per corner), when asked for.
    std::array<std::vector<float>, size_t(OverhangKind::Count)> triangles;
};
OverhangReport analyzeOverhangs(const MeshData &mesh, const OverhangOptions &o, bool wantTriangles);

} // namespace cad
