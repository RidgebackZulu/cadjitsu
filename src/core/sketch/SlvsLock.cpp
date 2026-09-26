#include "sketch/SlvsLock.h"

namespace cad {

std::mutex &slvsMutex() {
    static std::mutex m;
    return m;
}

} // namespace cad
