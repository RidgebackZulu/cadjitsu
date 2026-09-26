#pragma once

#include "doc/Feature.h"
#include "topo/Refs.h"

namespace cad {

// Rounds edges. Selecting a face fillets all of its edges. Edges that meet
// tangentially are followed automatically (tangent chain), as in Fusion.
class FilletFeature : public Feature {
public:
    std::vector<TopoRef> edges;
    std::vector<TopoRef> faces;
    ParamSlot radius;

    FeatureType type() const override { return FeatureType::Fillet; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<FilletFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

// Resolves edge and face references into edges grouped by body.
struct EdgeSelection {
    std::map<BodyId, std::vector<TopoDS_Edge>> byBody;
};
EdgeSelection resolveEdgeSelection(const ModelState &state, const std::vector<TopoRef> &edges,
                                   const std::vector<TopoRef> &faces, Status &status);

} // namespace cad
