#pragma once

#include "doc/Feature.h"
#include "topo/Refs.h"

namespace cad {

enum class ShellDirection { Inside, Outside };

// Hollows bodies out, leaving walls of an even thickness: the classic
// enclosure / container step. The picked faces are removed (the openings,
// e.g. a box's top); a body picked without faces is hollowed with a closed
// void inside. Inside keeps the outer size; Outside keeps the inner one (the
// walls grow outwards).
class ShellFeature : public Feature {
public:
    std::vector<TopoRef> faces;    // faces to remove (their bodies are shelled)
    std::vector<BodyId> bodies;    // bodies to hollow without an opening
    ParamSlot thickness;
    ShellDirection direction = ShellDirection::Inside;

    FeatureType type() const override { return FeatureType::Shell; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<ShellFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

// Walls thinner than this print poorly (less than two 0.4 mm extrusion lines).
inline constexpr double kThinWall = 0.8;

} // namespace cad
