#pragma once

#include "doc/Feature.h"
#include "topo/Refs.h"

#include <gp_Ax3.hxx>

namespace cad {

// How the plane is turned:
// - LocalX: "plane axes": tilted by `angle` about its own X axis, then by
//   `angleY` about its (tilted) Y axis;
// - LocalY: files from before two-axis tilts: `angle` about the Y axis;
// - Edge: `angle` about a straight edge.
enum class PlaneRotationAxis { LocalX, LocalY, Edge };

// The plane `frame` tilted by `ax` about its X axis and then `ay` about its new
// Y axis, both through `pivot` (radians). Shared with the UI's rotation rings.
gp_Ax3 tiltedFrame(const gp_Ax3 &frame, const gp_Pnt &pivot, double ax, double ay);

// An offset construction plane that can also be tilted by any angle about its
// own in-plane axes (one or both) or about a straight edge.
class ConstructionPlaneFeature : public Feature {
public:
    PlaneRef base;
    ParamSlot offset;  // along the base normal
    ParamSlot angle;   // tilt about X (or the edge)
    ParamSlot angleY;  // tilt about Y (LocalX mode)
    PlaneRotationAxis axis = PlaneRotationAxis::LocalX;
    TopoRef axisEdge;  // when axis == Edge
    // Turn about the plane's visible centre (new planes) rather than its frame
    // origin (planes saved before two-axis tilts, which keep their geometry).
    bool pivotAtCenter = true;

    FeatureType type() const override { return FeatureType::ConstructionPlane; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<ConstructionPlaneFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    std::vector<FeatureId> dependencies() const override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;

    // The base plane's frame and the plane's centre on it (no offset or tilt):
    // where the UI draws its arrow and rings. False if the base cannot be resolved.
    bool baseFrame(const ModelState &input, gp_Ax3 &frame, gp_Pnt &centre, Status &st) const;
};

} // namespace cad
