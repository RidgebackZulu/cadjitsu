#pragma once

#include <string>

namespace cad {

// Cadjitsu version string, e.g. "0.1.0".
const char *version();

// OpenCASCADE version the core was compiled against, e.g. "7.9.3".
std::string occtVersion();

} // namespace cad
