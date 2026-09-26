#pragma once

#include "doc/Feature.h"
#include "sketch/Sketch.h"
#include "topo/Refs.h"

namespace cad {

class SketchFeature : public Feature {
public:
    PlaneRef plane;
    Sketch sketch;

    FeatureType type() const override { return FeatureType::Sketch; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<SketchFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    std::vector<FeatureId> dependencies() const override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

} // namespace cad
