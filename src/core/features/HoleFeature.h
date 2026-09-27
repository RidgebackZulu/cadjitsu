#pragma once

#include "doc/Feature.h"
#include "features/ExtrudeFeature.h"
#include "features/ThreadFeature.h"
#include "topo/Refs.h"

namespace cad {

enum class HoleType { Simple, Counterbore, Countersink, Tapped };

// Drilled holes placed on a planar face (at points in the face's plane
// coordinates) or at sketch points. The drill tip is flat or conical.
class HoleFeature : public Feature {
public:
    // Placement: a planar face plus points, or a sketch plus point entities.
    TopoRef face;
    std::vector<Vec2> points;       // in frameForPlane(face) coordinates
    FeatureId sketch = kNoFeature;
    std::vector<int> sketchPoints;

    HoleType holeType = HoleType::Simple;
    ExtentType extent = ExtentType::Distance; // Distance or ThroughAll
    ParamSlot diameter;
    ParamSlot depth;
    ParamSlot cboreDiameter;
    ParamSlot cboreDepth;
    ParamSlot csinkDiameter;
    ParamSlot csinkAngle;
    ParamSlot tipAngle;             // drill point angle (e.g. 118 deg)
    bool flatTip = false;
    // Tapped: the drill is the thread's tap drill (the diameter is not used),
    // and the thread is modelled into the bore (see ThreadFeature).
    std::string thread;             // e.g. "M5", "1/4-20 UNC"
    ThreadMode threadMode = ThreadMode::Auto;
    ParamSlot threadClearance;      // radial, mm (default 0.15)

    FeatureType type() const override { return FeatureType::Hole; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<HoleFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    std::vector<FeatureId> dependencies() const override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

} // namespace cad
