#include "features/SketchFeature.h"

#include "sketch/SketchText.h"

#include "sketch/ProfileBuilder.h"
#include "sketch/SketchProject.h"
#include "sketch/SketchSolver.h"
#include "topo/Resolver.h"

namespace cad {

std::vector<ParamDef> SketchFeature::params() const {
    std::vector<ParamDef> out;
    for(const auto &c : sketch.constraints) {
        if(!isDimension(c.type) || c.param.empty()) continue;
        ParamDef d;
        d.name = c.param;
        d.expr = c.expr;
        d.kind = c.type == SkCon::Angle ? ValueKind::Angle : ValueKind::Length;
        d.owner = id;
        d.label = name + " " + toString(c.type) + " dimension";
        out.push_back(std::move(d));
    }
    return out;
}

json SketchFeature::dataToJson() const { return json{{"plane", plane.toJson()}, {"sketch", sketch.toJson()}}; }

void SketchFeature::dataFromJson(const json &j) {
    plane = PlaneRef::fromJson(j.value("plane", json()));
    sketch = Sketch::fromJson(j.value("sketch", json::object()));
}

std::vector<FeatureId> SketchFeature::dependencies() const {
    std::vector<FeatureId> out = projectionSources(sketch);
    if(plane.kind == PlaneRef::Kind::Construction) out.push_back(plane.plane);
    return out;
}

FeatureResult SketchFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status status;
        gp_Ax3 frame;
        if(!resolvePlane(*input, plane, frame, status)) return {input, status};

        auto result = std::make_shared<SketchResult>();
        result->feature = id;
        result->name = name;
        result->frame = frame;
        result->sketch = sketch;

        // Projected geometry follows the sketches it came from.
        std::vector<std::string> warnings;
        refreshProjections(
            result->sketch, frame,
            [&](FeatureId src) -> const SketchResult * {
                auto it = input->sketches.find(src);
                return it == input->sketches.end() ? nullptr : it->second.get();
            },
            warnings);
        for(const std::string &w : warnings) status.merge(Status::warning(w));

        // Evaluate dimensions and solve the constraints.
        SolveOptions options;
        options.computeFreeEntities = false; // only needed while editing the sketch
        SolveOutcome solved = solveSketch(
            result->sketch,
            [&](const std::string &param, double &value) {
                if(!ctx.params) return false;
                const ParamValue *v = ctx.params->find(param);
                if(!v || !v->ok) return false;
                value = v->value;
                return true;
            },
            options);
        result->dof = solved.dof;
        result->failedConstraints = solved.failed;
        result->freeEntities = solved.freeEntities;
        if(!solved.ok) status.merge(Status::warning(solved.message));
        result->status = status;

        result->profiles = sketchProfiles(result->sketch);
        auto out = std::make_shared<ModelState>(*input);
        out->sketches[id] = std::move(result);
        return {out, status};
    });
}

} // namespace cad
