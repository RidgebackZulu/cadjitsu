#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class TopoDS_Shape;

namespace cad {

class NamedShape;

// Display tessellation of one body. Triangles of each B-rep face and points of
// each edge polyline are contiguous, so a face or edge can be drawn (or
// highlighted) as a single index range.
struct MeshData {
    struct Range {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    std::vector<float> positions; // xyz per vertex
    std::vector<float> normals;   // xyz per vertex
    std::vector<uint32_t> indices;
    std::vector<Range> faceRanges; // [face index - 1] -> range in `indices`
    std::vector<uint32_t> triangleFace; // [triangle] -> face index (1-based)

    std::vector<float> edgePoints;   // xyz per polyline point
    std::vector<Range> edgeRanges;   // [edge index - 1] -> range of points (a line strip)

    std::vector<float> vertexPoints; // [vertex index - 1] -> xyz

    float bboxMin[3] = {0, 0, 0};
    float bboxMax[3] = {0, 0, 0};

    size_t vertexCount() const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size() / 3; }
};

// A closed triangle mesh with shared (welded) vertices, for STL export.
struct TriMesh {
    std::vector<std::array<double, 3>> vertices;
    std::vector<std::array<uint32_t, 3>> triangles;
};

// Automatic display deflection for a shape of the given size (mm).
double defaultDeflection(double modelSize);

// Tessellates a copy of `shape` (never touching the caller's triangulation) for display.
std::shared_ptr<MeshData> tessellateForDisplay(const NamedShape &shape, double deflection,
                                               double angularDeflection = 0.35);

// Watertight mesh: vertices on shared B-rep edges are shared between faces.
// Returns false (with a message) if the shape could not be meshed.
// Hairline faces the mesher cannot triangulate (strips narrower than 1 um
// that booleans leave where two faces nearly line up) are left out; the
// widest one is reported in `hairlineGap` so the caller can weld across it.
bool weldedMesh(const TopoDS_Shape &shape, double deflection, double angularDeflection, TriMesh &out,
                std::string &error, double *hairlineGap = nullptr);

// Merges vertices closer than `tolerance` (fallback welding) and drops
// triangles that became degenerate.
void weldByDistance(TriMesh &mesh, double tolerance);

} // namespace cad
