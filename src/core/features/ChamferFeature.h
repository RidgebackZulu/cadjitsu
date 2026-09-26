#pragma once

#include "doc/Feature.h"
#include "topo/Refs.h"

namespace cad {

enum class ChamferType { EqualDistance, TwoDistances, DistanceAngle };

class ChamferFeature : public Feature {
public:
    std::vector<TopoRef> edges;
    std::vector<TopoRef> faces;
    ChamferType chamferType = ChamferType::EqualDistance;
    ParamSlot distance;   // distance (or first distance)
    ParamSlot distance2;  // second distance (TwoDistances)
    ParamSlot angle;      // angle (DistanceAngle)
    bool flip = false;    // swap which side the first distance / angle is measured on

    FeatureType type() const override { return FeatureType::Chamfer; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<ChamferFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

} // namespace cad
