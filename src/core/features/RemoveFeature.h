#pragma once

#include "doc/Feature.h"

#include <vector>

namespace cad {

// Remove (Fusion's): bodies deleted from this point of the timeline on. The
// features that made them stay, so undoing, deleting or suppressing this
// step, or rolling back before it, brings them back.
class RemoveFeature : public Feature {
public:
    std::vector<BodyId> bodies;

    FeatureType type() const override { return FeatureType::Remove; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<RemoveFeature>(*this); }
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

} // namespace cad
