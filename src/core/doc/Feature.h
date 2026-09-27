#pragma once

#include "base/Ids.h"
#include "base/Json.h"
#include "base/Status.h"
#include "doc/ModelState.h"
#include "expr/ParamTable.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cad {

enum class FeatureType { Sketch, Extrude, Fillet, Chamfer, Hole, Combine, ConstructionPlane, Split, Draft, Pattern };

const char *toString(FeatureType t);
bool featureTypeFromString(const std::string &s, FeatureType &out);
// Default name stem shown in the timeline ("Extrude" -> "Extrude3").
const char *displayStem(FeatureType t);

// A feature's numeric input stored as an expression bound to a parameter name.
struct ParamSlot {
    std::string name; // "d7"
    std::string expr; // "20 mm"

    json toJson() const { return json{{"name", name}, {"expr", expr}}; }
    static ParamSlot fromJson(const json &j) {
        ParamSlot s;
        if(j.is_object()) {
            s.name = jget<std::string>(j, "name", "");
            s.expr = jget<std::string>(j, "expr", "");
        }
        return s;
    }
    bool empty() const { return name.empty(); }
};

class Feature;

struct ComputeContext {
    const ParamTable *params = nullptr;
    const std::atomic<bool> *cancel = nullptr;
    // The timeline being evaluated and this feature's place in it (patterns
    // look up the features they repeat, which must come before them).
    const std::vector<std::shared_ptr<const Feature>> *timeline = nullptr;
    size_t index = 0;

    bool cancelled() const { return cancel && cancel->load(std::memory_order_relaxed); }
    // Looks up an evaluated parameter; on failure fills `status` with an error.
    // Slots not registered in the table are evaluated directly as `kind`.
    bool value(const ParamSlot &slot, double &out, Status &status, ValueKind kind = ValueKind::Length) const;
};

struct FeatureResult {
    FeatureResult() = default;
    FeatureResult(StatePtr s, Status st, std::shared_ptr<const Body> t = nullptr)
        : state(std::move(s)), status(std::move(st)), tool(std::move(t)) {}

    StatePtr state;
    Status status;
    // Display only: the material a cut or hole removes, shown translucent
    // while the feature is previewed.
    std::shared_ptr<const Body> tool;
};

// A timeline entry. Features are immutable values once in the timeline
// (shared between undo snapshots and threads); editing clones and replaces.
class Feature {
public:
    virtual ~Feature() = default;

    FeatureId id = kNoFeature;
    std::string name;
    bool suppressed = false;

    virtual FeatureType type() const = 0;
    // What new features of this kind are called ("Extrude" -> Extrude1).
    virtual std::string nameStem() const { return displayStem(type()); }
    virtual std::shared_ptr<Feature> clone() const = 0;

    // Numeric inputs exposed as model parameters.
    virtual std::vector<ParamDef> params() const { return {}; }
    // Type-specific data (everything except id / name / suppressed).
    virtual json dataToJson() const = 0;
    virtual void dataFromJson(const json &j) = 0;
    // Other features this one reads (sketches, construction planes).
    virtual std::vector<FeatureId> dependencies() const { return {}; }

    // Applies the feature to `input`. Must not throw; failures are reported
    // as an Error status (the returned state then equals the input).
    virtual FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const = 0;

    json toJson() const;
    static std::shared_ptr<Feature> fromJson(const json &j, std::string *error = nullptr);
    static std::shared_ptr<Feature> create(FeatureType type);
};

using FeaturePtr = std::shared_ptr<const Feature>;

// Runs `fn` guarding against OCCT exceptions; errors become a failed result
// that passes `input` through unchanged.
FeatureResult guardedCompute(const StatePtr &input, const std::function<FeatureResult()> &fn);

} // namespace cad
