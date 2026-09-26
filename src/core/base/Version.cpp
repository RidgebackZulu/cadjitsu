#include "base/Version.h"

#include <Standard_Version.hxx>

namespace cad {

const char *version() { return CADLY_VERSION; }

std::string occtVersion() { return OCC_VERSION_COMPLETE; }

} // namespace cad
