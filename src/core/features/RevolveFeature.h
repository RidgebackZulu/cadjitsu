#pragma once

#include "doc/Feature.h"
#include "features/BodyOps.h"
#include "features/PatternFeature.h"
#include "topo/Refs.h"

#include <gp_Ax1.hxx>

#include <optional>

namespace cad {

// The axis a Revolve turns about: a line of a sketch (or that sketch's own X /
// Y axis), an origin axis, or a straight edge / circle / cylinder of a body.
struct RevolveAxis {
    PatternAxis axis;     // origin axis or body edge / face
    FeatureId sketch = 0; // or: a line of this sketch...
    int line = 0;         // ...its entity id (kSketchXAxis / kSketchYAxis: the sketch's axes)

    json toJson() const;
    static RevolveAxis fromJson(const json &j);
    bool empty() const { return axis.empty() && !sketch; }
};
std::optional<gp_Ax1> resolveRevolveAxis(const ModelState &state, const RevolveAxis &a, Status &status);

enum class RevolveExtent { OneSide, Symmetric, TwoSides };

// Turns sketch profiles (or planar faces) about an axis into a solid of
// revolution: knobs, bottles, spacers, pulleys. 360 degrees makes a closed
// solid; less leaves start and end faces. Symmetric turns the angle each way;
// Two Sides takes a second angle the other way. The result is a new body, or
// joins, cuts or intersects bodies, as Extrude does.
class RevolveFeature : public Feature {
public:
    std::vector<ProfileRef> profiles;
    std::vector<TopoRef> faces;
    RevolveAxis axis;
    RevolveExtent extent = RevolveExtent::OneSide;
    ParamSlot angle;   // side 1 (default 360 deg)
    ParamSlot angle2;  // side 2 (two sides)
    bool flip = false; // turn the other way round

    BodyOperation operation = BodyOperation::NewBody;
    std::vector<BodyId> participants;

    FeatureType type() const override { return FeatureType::Revolve; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<RevolveFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    std::vector<FeatureId> dependencies() const override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

const char *toString(RevolveExtent e);
RevolveExtent revolveExtentFromString(const std::string &s);

} // namespace cad
