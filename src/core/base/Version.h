#pragma once

#include <string>

namespace cad {

// Cadjitsu version, e.g. "1.0.0" (a CI build between releases: "1.0.1-dev.52").
const char *version();

// OpenCASCADE version the core was compiled against, e.g. "7.9.3".
std::string occtVersion();

} // namespace cad
