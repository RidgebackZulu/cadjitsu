#include "base/KernelLock.h"

namespace cad {

std::recursive_mutex &kernelMutex() {
    static std::recursive_mutex m;
    return m;
}

} // namespace cad
