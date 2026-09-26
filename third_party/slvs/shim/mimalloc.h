// Minimal stand-in for the mimalloc heap API used by SolveSpace's temporary
// arena (src/platform/platformbase.cpp). Each heap records its blocks and frees
// them all on destroy, which is exactly the semantics the arena relies on.
#pragma once
#include <cstdlib>
#include <vector>

struct mi_heap_s {
    std::vector<void *> blocks;
};
typedef struct mi_heap_s mi_heap_t;

inline mi_heap_t *mi_heap_new() { return new mi_heap_t(); }

inline void mi_heap_destroy(mi_heap_t *heap) {
    if(heap == nullptr) return;
    for(void *p : heap->blocks) std::free(p);
    delete heap;
}

inline void *mi_heap_zalloc(mi_heap_t *heap, size_t size) {
    void *p = std::calloc(1, size ? size : 1);
    if(p != nullptr) heap->blocks.push_back(p);
    return p;
}
