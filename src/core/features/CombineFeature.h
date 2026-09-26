#pragma once

#include "doc/Feature.h"
#include "features/ExtrudeFeature.h"

namespace cad {

// Joins, cuts or intersects a target body with tool bodies. With keepTools the
// tool bodies remain in the model, like Fusion's "Keep Tools" option.
class CombineFeature : public Feature {
public:
    BodyId target;
    std::vector<BodyId> tools;
    BodyOperation operation = BodyOperation::Join; // Join, Cut or Intersect
    bool keepTools = false;

    FeatureType type() const override { return FeatureType::Combine; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<CombineFeature>(*this); }
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

} // namespace cad
