#pragma once

#include "doc/Feature.h"
#include "features/ThreadTable.h"
#include "topo/NamedShape.h"
#include "topo/Refs.h"

#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>

namespace cad {

// A countersink at a threaded hole's open ends, for a screw to start on.
struct Countersink {
    bool on = true;
    double angle = 90;    // included, degrees (82, 90, 100, 120 are usual)
    double diameter = 0;  // at the face, mm; 0: a little over the thread's major diameter
};

// Threads cylindrical faces: the bores of holes (internal threads, to take a
// screw) or posts (external, a bolt). The size comes from the cylinder's
// diameter unless given; any hole or post takes any size, fitted to it on its
// own axis (a hole too big is filled and bored again, a post too thin grown).
// The print clearance opens internal threads and shrinks external ones so
// printed parts fit.
class ThreadFeature : public Feature {
public:
    std::vector<TopoRef> faces;
    std::string size;       // e.g. "M6", "1/4-20 UNC"; empty: from each face's diameter
    ParamSlot clearance;    // radial, mm (default 0.15)
    ParamSlot length;       // from the open end; empty: the whole face
    bool leftHand = false;
    // A hole's countersink at its open ends.
    bool countersink = true;
    double countersinkAngle = 90;   // included, degrees
    ParamSlot countersinkDiameter;  // empty: a little over the major diameter

    FeatureType type() const override { return FeatureType::Thread; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<ThreadFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

// Threads `body` along `frame`'s Z axis from z = 0 to `length`: a hole's wall
// (internal) or a post (external), `outer` its radius. The hole or post is
// first fitted to the thread whatever its size. Internal threads open by
// `clearance`, external ones shrink by it. False with `why` on failure.
// An end the thread leaves the part through (`openStart` at z = 0, `openEnd`
// at `length`) has the full thread running out through it, with `csk` (a
// hole) or a 45 degree chamfered tip (a post) for a screw or nut to start on;
// a closed end (a blind hole's bottom, a shoulder, the inner end of a
// part-length thread) has the thread fading out over two turns.
bool threadBody(const NamedShape &body, const ThreadSpec &spec, const gp_Ax3 &frame, double length, bool internal,
                double clearance, bool leftHand, double outer, const std::string &prefix, NamedShape &result,
                std::string &why, bool openStart = true, bool openEnd = true, const Countersink &csk = {});

} // namespace cad
