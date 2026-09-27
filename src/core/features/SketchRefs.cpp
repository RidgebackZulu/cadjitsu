#include "features/SketchRefs.h"

#include "features/ExtrudeFeature.h"
#include "topo/Resolver.h"

#include <cmath>

namespace cad {

int refreshProfileRefs(Document &doc, FeatureId sketch) {
    const int i = doc.indexOf(sketch);
    if(i < 0) return 0;
    const StatePtr st = doc.stateAt(i + 1);
    if(!st) return 0;
    auto sk = st->sketches.find(sketch);
    if(sk == st->sketches.end()) return 0;
    int changed = 0;
    const std::vector<FeaturePtr> features = doc.features();
    for(size_t k = size_t(i) + 1; k < features.size(); ++k) {
        auto e = std::dynamic_pointer_cast<const ExtrudeFeature>(features[k]);
        if(!e) continue;
        auto copy = std::static_pointer_cast<ExtrudeFeature>(e->clone());
        bool any = false;
        for(ProfileRef &r : copy->profiles) {
            if(r.sketch != sketch) continue;
            const Profile *p = sk->second->profileByKey(r.key);
            // Only when the region has left its stored inside point: otherwise
            // the reference still works as it is, and the model is not recomputed.
            if(!p || p->contains(r.sample)) continue;
            const ProfileRef before = r;
            r.sample = p->sample;
            captureProfileOutline(*st, r);
            const bool moved = std::hypot(before.sample.x - r.sample.x, before.sample.y - r.sample.y) > 1e-9;
            any |= moved || before.outline.size() != r.outline.size() ||
                   (!r.outline.empty() && std::hypot(before.outline[0].x - r.outline[0].x,
                                                     before.outline[0].y - r.outline[0].y) > 1e-9);
        }
        if(any) {
            doc.replaceFeature(copy, {}, false);
            ++changed;
        }
    }
    return changed;
}

} // namespace cad
