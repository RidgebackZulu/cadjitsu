#pragma once

#include "io/StepIO.h"
#include "io/StlWriter.h"

#include <string>
#include <vector>

namespace cad {
class Document;
}

namespace cadly {

class ModelView;

// What to export: the bodies (resolved on the UI thread, so the rest can run
// on any thread) and the options for each format.
struct ExportJob {
    enum class Format { Stl, Step };
    Format format = Format::Stl;
    std::vector<cad::NamedSolid> solids;
    cad::StlOptions stl;
    cad::StepSchema schema = cad::StepSchema::AP242;
};

// The bodies to export: the selected ones (bodies, or the bodies of selected
// faces / edges) or every visible body.
ExportJob makeExportJob(const ModelView &view, const cad::Document &doc, ExportJob::Format format, bool selectedOnly);

// What an export did.
struct ExportResult {
    bool ok = false;           // written
    std::string error;
    // STL: the mesh's printability check (written only if it passes, unless forced).
    bool meshChecked = false;
    cad::MeshReport report;
    double solidVolume = 0.0;  // exact B-rep volume of what was exported
    // STEP: read back and measured.
    bool reimported = false;
    double reimportedVolume = 0.0;
};

// STL: meshes and checks the bodies; `path` is written only if the mesh
// passes (or `writeInvalid`). STEP: writes and reads the file back.
ExportResult runExport(const ExportJob &job, const std::string &path, bool writeInvalid = false);

} // namespace cadly
