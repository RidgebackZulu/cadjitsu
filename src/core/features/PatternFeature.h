#pragma once

#include "doc/Feature.h"
#include "topo/Refs.h"

#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>

#include <optional>
#include <vector>

namespace cad {

enum class PatternKind { Mirror, Rectangular, Circular };

// A direction or an axis: X, Y or Z through the origin, or a model entity (a
// straight edge; a circle, arc, cylinder or cone gives its axis).
struct PatternAxis {
    std::string builtin; // "x", "y", "z", or empty for `ref`
    TopoRef ref;

    json toJson() const;
    static PatternAxis fromJson(const json &j);
    bool empty() const { return builtin.empty() && ref.empty(); }
};
std::optional<gp_Ax1> resolveAxis(const ModelState &state, const PatternAxis &a, Status &status);

// Mirror, Rectangular Pattern and Circular Pattern, of bodies or of features.
// Bodies are copied (and joined to the original, or kept as new bodies);
// features (holes, extrudes) are repeated: their tool solids are copied and
// applied the same way (cut, join or new body), so a hole becomes a row or a
// bolt circle of holes. Copies listed in `skip` are left out.
class PatternFeature : public Feature {
public:
    PatternKind kind = PatternKind::Rectangular;
    std::vector<BodyId> bodies;         // pattern bodies, or...
    std::vector<FeatureId> features;    // ...these features (earlier in the timeline)
    // Mirror
    PlaneRef plane;
    // Rectangular: one or two directions (count includes the original).
    PatternAxis dir1, dir2;
    ParamSlot count1, spacing1, count2, spacing2;
    // Circular
    PatternAxis axis;
    ParamSlot count, angle; // total angle; 360 spreads the copies evenly all round
    bool symmetric = false; // circular: spread both ways from the original
    bool join = true;       // bodies: join the copies to the original body
    std::vector<int> skip;  // copy numbers (1 = the first copy) left out

    FeatureType type() const override { return FeatureType::Pattern; }
    std::string nameStem() const override {
        return kind == PatternKind::Mirror ? "Mirror" : kind == PatternKind::Circular ? "CircularPattern" : "RectangularPattern";
    }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<PatternFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    std::vector<FeatureId> dependencies() const override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;

    // The placements of the copies (not the original), in order; false with
    // `status` if the inputs do not resolve.
    bool transforms(const ModelState &state, const ComputeContext &ctx, std::vector<gp_Trsf> &out,
                    Status &status) const;
};

const char *toString(PatternKind k);
PatternKind patternKindFromString(const std::string &s);

} // namespace cad
