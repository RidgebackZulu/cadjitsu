#pragma once

#include "mesh/MeshData.h"
#include "mesh/MeshValidator.h"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace cad {

enum class StlResolution { Coarse, Medium, Fine, Custom };

struct StlOptions {
    StlResolution resolution = StlResolution::Medium;
    double deflection = 0.02;       // mm, used when resolution == Custom
    double angleDegrees = 15.0;     // used when resolution == Custom
    bool binary = true;
    bool mergeBodies = true;        // boolean-union all bodies into one watertight shell set
};

// Chordal deflection (mm) and angular deflection (deg) for a resolution.
void stlTolerances(const StlOptions &o, double &deflection, double &angleDegrees);

struct StlExport {
    TriMesh mesh;
    MeshReport report;
    double solidVolume = 0.0; // exact B-rep volume
};

// Meshes solids for printing: optionally merges them, welds vertices along
// shared B-rep edges, then validates (watertight, oriented, volume).
bool buildStlMesh(const std::vector<TopoDS_Shape> &solids, const StlOptions &options, StlExport &out,
                  std::string &error);

bool writeStlFile(const std::string &path, const TriMesh &mesh, bool binary, const std::string &name,
                  std::string &error);

// Reads a binary or ASCII STL (used by tests and for verification).
bool readStlFile(const std::string &path, TriMesh &mesh, std::string &error);

} // namespace cad
