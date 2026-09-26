#pragma once

#include "doc/Feature.h"
#include "topo/Refs.h"

namespace cad {

enum class PlaneRotationAxis { LocalX, LocalY, Edge };

// An offset construction plane that can also be rotated by any angle about one
// of its own in-plane axes or about a straight edge.
class ConstructionPlaneFeature : public Feature {
public:
    PlaneRef base;
    ParamSlot offset;  // along the base normal
    ParamSlot angle;   // rotation angle
    PlaneRotationAxis axis = PlaneRotationAxis::LocalX;
    TopoRef axisEdge;  // when axis == Edge

    FeatureType type() const override { return FeatureType::ConstructionPlane; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<ConstructionPlaneFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    std::vector<FeatureId> dependencies() const override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

} // namespace cad
