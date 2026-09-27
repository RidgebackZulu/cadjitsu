#pragma once

#include "doc/Feature.h"
#include "features/ThreadTable.h"
#include "topo/NamedShape.h"
#include "topo/Refs.h"

#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>

namespace cad {

// How a thread is made: real helical geometry, or only the right bore (the
// tap drill for a hole, the major diameter for a boss) to tap or die-cut, or
// to take a self-tapping screw. Auto models M5 / #10 and bigger.
enum class ThreadMode { Auto, Modeled, TapDrill };

// Threads cylindrical faces: the bores of holes (internal threads, to take a
// screw) or bosses (external, a bolt). The size comes from the cylinder's
// diameter unless given; the print clearance opens internal threads and
// shrinks external ones so printed parts fit.
class ThreadFeature : public Feature {
public:
    std::vector<TopoRef> faces;
    std::string size;       // e.g. "M6", "1/4-20 UNC"; empty: from each face's diameter
    ThreadMode mode = ThreadMode::Auto;
    ParamSlot clearance;    // radial, mm (default 0.15)
    ParamSlot length;       // from the open end; empty: the whole face
    bool leftHand = false;

    FeatureType type() const override { return FeatureType::Thread; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<ThreadFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

// Threads `body` along `frame`'s Z axis from z = 0 to `length`: a hole's wall
// (internal) or a boss (external, `outer` its radius). Internal threads open by
// `clearance`, external ones shrink by it. False with `why` on failure.
bool threadBody(const NamedShape &body, const ThreadSpec &spec, const gp_Ax3 &frame, double length, bool internal,
                double clearance, bool leftHand, double outer, const std::string &prefix, NamedShape &result,
                std::string &why);

const char *toString(ThreadMode m);
ThreadMode threadModeFromString(const std::string &s);

} // namespace cad
