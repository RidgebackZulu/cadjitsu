#pragma once

#include "doc/Feature.h"
#include "topo/Refs.h"

#include <vector>

namespace cad {

enum class SplitTool { Plane, Sketch };
// Which pieces to keep: both, or only those on the side the plane's normal
// points to ("front") or the other ("back").
enum class SplitKeep { Both, Front, Back };

// Cuts bodies in two (or more) with an unbounded plane (an origin plane, a
// construction plane or a planar face) or with sketch curves swept both ways
// along the sketch normal. Each piece becomes its own body: the biggest keeps
// the body's name. For printing parts bigger than the bed, optional alignment
// pin holes are drilled into both sides of a plane cut.
class SplitFeature : public Feature {
public:
    std::vector<BodyId> bodies; // empty: every body the tool crosses
    SplitTool tool = SplitTool::Plane;
    PlaneRef plane;
    FeatureId sketch = kNoFeature;
    std::vector<int> curves; // sketch curve ids; empty: every non-construction curve
    SplitKeep keep = SplitKeep::Both;
    bool pins = false;
    ParamSlot pinDiameter; // hole diameter (a printed or cut-filament pin plus clearance)
    ParamSlot pinDepth;    // into each side

    FeatureType type() const override { return FeatureType::Split; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<SplitFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    std::vector<FeatureId> dependencies() const override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

} // namespace cad
