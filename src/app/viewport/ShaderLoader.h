#pragma once

#include <rhi/qshader.h>

#include <QString>

namespace cadly {

// Loads a compiled shader (e.g. "mesh.vert") from the ":/shaders/<name>.qsb"
// resource produced by qt_add_shaders. Aborts with a clear message if missing.
QShader loadShader(const QString &name);

} // namespace cadly
