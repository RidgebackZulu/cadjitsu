#pragma once

#include "doc/Feature.h"
#include "geom/Align.h"
#include "topo/Refs.h"

#include <gp_Trsf.hxx>

#include <optional>
#include <vector>

namespace cad {

// One end of an alignment: a vertex, edge or face of the model, a plane (an
// origin or construction plane), or the origin, an origin axis ("x", "y",
// "z") or an origin plane ("xy", "xz", "yz"). `at` is where it was clicked,
// which picks the end of an edge.
struct AlignRef {
    std::string builtin;
    TopoRef ref;
    std::optional<PlaneRef> plane;
    std::optional<gp_Pnt> at;

    json toJson() const;
    static AlignRef fromJson(const json &j);
    bool empty() const { return builtin.empty() && ref.empty() && !plane; }
};

// The anchor a pick stands for (see alignTransform); nullopt with `status`
// if it does not resolve or gives none.
std::optional<AlignAnchor> anchorOf(const ModelState &state, const AlignRef &r, Status &status);

// Align: bodies moved so geometry picked on them (from) meets geometry picked
// elsewhere (to), as in Fusion. The bodies keep their face names, so later
// features still find their faces; `copy` leaves them and adds moved copies.
class AlignFeature : public Feature {
public:
    std::vector<BodyId> bodies;
    AlignRef from, to;
    bool flip = false;
    ParamSlot angle; // about the aligned line or normal; empty: 0
    bool copy = false;

    FeatureType type() const override { return FeatureType::Align; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<AlignFeature>(*this); }
    std::vector<ParamDef> params() const override;
    std::vector<FeatureId> dependencies() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;

    // The move, with the two anchors (for the dialog); false with `status`.
    bool placement(const ModelState &state, const ComputeContext &ctx, gp_Trsf &out, AlignAnchor *fromAnchor,
                   AlignAnchor *toAnchor, Status &status) const;
};

} // namespace cad
