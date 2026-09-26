#pragma once

#include "mesh/MeshData.h"

#include <string>
#include <vector>

namespace cad {

// Printability checks for a triangle mesh.
struct MeshReport {
    bool watertight = false;   // every edge shared by exactly two triangles, opposite directions
    bool ok = false;           // watertight, no degenerate triangles, positive volume, volume matches
    size_t vertices = 0;
    size_t triangles = 0;
    size_t shells = 0;
    size_t boundaryEdges = 0;      // used by one triangle (holes)
    size_t nonManifoldEdges = 0;   // used by more than two triangles
    size_t flippedEdges = 0;       // two triangles with inconsistent winding
    size_t degenerateTriangles = 0;
    double volume = 0.0;           // signed; > 0 for outward-facing normals
    double area = 0.0;
    std::vector<std::string> problems;

    std::string summary() const;
};

// `expectedVolume` (if > 0) is the exact B-rep volume the mesh must match
// within `volumeTolerance` (absolute, mm^3; <= 0 means 2% of the volume).
MeshReport validateMesh(const TriMesh &mesh, double expectedVolume = -1.0, double volumeTolerance = -1.0);

} // namespace cad
