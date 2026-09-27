#pragma once

#include "doc/Feature.h"
#include "topo/Refs.h"

#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>

#include <optional>

namespace cad {

// Tilts flat faces about a hinge edge, as a draft. The hinge is a straight
// edge of one of the faces; the face on its other side sets the pull
// direction (away from it), and the neutral plane through the hinge, square
// to that direction, stays put. A positive angle leans the faces in over the
// body (a taper that prints without support); Flip leans them out. Every
// selected face turns about the same neutral plane, so picking all the walls
// of a box with one bottom edge tapers the whole box.
class DraftFeature : public Feature {
public:
    std::vector<TopoRef> faces;
    TopoRef hinge;
    ParamSlot angle;
    bool flip = false;

    FeatureType type() const override { return FeatureType::Draft; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<DraftFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

// The draft's geometry for a face and its hinge edge (shared with the UI's
// ring): the hinge line, the pull direction and the neutral plane. The face
// must have `hinge` as one of its straight edges.
struct DraftFrame {
    gp_Ax1 hingeAxis;   // along the hinge, oriented so a positive turn leans the face in
    gp_Dir pull;
    gp_Pln neutral;
    gp_Dir up;          // in the face, square to the hinge, pointing away from it
    gp_Pnt middle;      // the hinge's middle
};
std::optional<DraftFrame> draftFrame(const TopoDS_Shape &body, const TopoDS_Face &face, const TopoDS_Edge &hinge,
                                     std::string *why = nullptr);

} // namespace cad
