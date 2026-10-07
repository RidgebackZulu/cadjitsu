#pragma once

#include "doc/Feature.h"
#include "features/BodyOps.h"
#include "topo/Refs.h"

namespace cad {

enum class ExtentType { Distance, ThroughAll, ToObject };
enum class ExtrudeDirection { OneSide, TwoSides, Symmetric };

class ExtrudeFeature : public Feature {
public:
    // Inputs: sketch profiles and/or planar body faces.
    std::vector<ProfileRef> profiles;
    std::vector<TopoRef> faces;

    ExtrudeDirection direction = ExtrudeDirection::OneSide;
    ExtentType extent = ExtentType::Distance;  // side 1 (and symmetric)
    ExtentType extent2 = ExtentType::Distance; // side 2 (two sides)
    ParamSlot distance;    // side 1 length (negative flips)
    ParamSlot distance2;   // side 2 length
    ParamSlot taper;       // side 1 taper angle (optional)
    ParamSlot taper2;      // side 2 taper angle (optional)
    TopoRef toObject;      // ToObject target for side 1
    TopoRef toObject2;     // ToObject target for side 2
    bool flip = false;

    BodyOperation operation = BodyOperation::NewBody;
    std::vector<BodyId> participants; // bodies to join / cut / intersect; empty = automatic

    FeatureType type() const override { return FeatureType::Extrude; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<ExtrudeFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    std::vector<FeatureId> dependencies() const override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

} // namespace cad
