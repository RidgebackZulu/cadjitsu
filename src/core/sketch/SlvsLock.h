#pragma once

#include <mutex>

namespace cad {

// libslvs keeps its solver state in globals, so every call into it from any
// thread must hold this lock.
std::mutex &slvsMutex();

} // namespace cad
