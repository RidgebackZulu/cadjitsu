#pragma once

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace cad {

struct NamedSolid {
    std::string name;
    TopoDS_Shape shape;
    double rgb[3] = {0.62, 0.66, 0.72};
};

enum class StepSchema { AP214, AP242 };

// Writes solids (with names and colours) to a STEP file in millimetres.
bool writeStepFile(const std::string &path, const std::vector<NamedSolid> &solids, StepSchema schema,
                   std::string &error);

// Reads the named top-level shapes of a STEP file.
bool readStepFile(const std::string &path, std::vector<NamedSolid> &solids, std::string &error);

} // namespace cad
