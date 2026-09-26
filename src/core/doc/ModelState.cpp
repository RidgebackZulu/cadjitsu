#include "doc/ModelState.h"

#include "base/KernelLock.h"
#include "geom/OcctUtil.h"
#include "mesh/MeshData.h"

#include <algorithm>

namespace cad {

std::shared_ptr<const MeshData> Body::mesh(double deflection) const {
    std::call_once(m_meshOnce, [&] {
        const KernelLock lock(kernelMutex());
        const double d = deflection > 0 ? deflection : defaultDeflection(bboxDiagonal(shape.shape()));
        m_mesh = tessellateForDisplay(shape, d);
    });
    return m_mesh;
}

BodyId ModelState::resolveBodyId(BodyId id) const {
    for(int guard = 0; guard < 1000; ++guard) {
        if(bodies.count(id)) return id;
        auto it = mergedInto.find(id);
        if(it == mergedInto.end()) return {};
        id = it->second;
    }
    return {};
}

const Body *ModelState::body(const BodyId &id) const {
    const BodyId real = resolveBodyId(id);
    auto it = bodies.find(real);
    return it == bodies.end() ? nullptr : it->second.get();
}

std::vector<const Body *> ModelState::orderedBodies() const {
    std::vector<const Body *> out;
    for(const auto &kv : bodies) out.push_back(kv.second.get());
    std::sort(out.begin(), out.end(), [](const Body *a, const Body *b) { return a->order < b->order; });
    return out;
}

double ModelState::modelSize() const {
    Bnd_Box box;
    for(const auto &kv : bodies) {
        Bnd_Box b = boundingBox(kv.second->shape.shape());
        if(!b.IsVoid()) box.Add(b);
    }
    if(box.IsVoid()) return 100.0;
    return std::max(1.0, std::sqrt(box.SquareExtent()));
}

} // namespace cad
