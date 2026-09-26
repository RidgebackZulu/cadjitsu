#pragma once

#include <mutex>

namespace cad {

// Serializes geometry-kernel work (feature computation, tessellation, export)
// between the UI thread and background recompute. Model states are immutable
// and shared, but some OCCT algorithms annotate their input shapes (pcurves,
// tolerances), so two threads must not run them on the same shapes at once.
// Recursive, so helpers can lock again inside a locked region.
std::recursive_mutex &kernelMutex();

using KernelLock = std::lock_guard<std::recursive_mutex>;

} // namespace cad
