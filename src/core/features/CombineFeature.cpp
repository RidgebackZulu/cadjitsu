#include "features/CombineFeature.h"

#include "features/BodyOps.h"
#include "geom/OcctUtil.h"

#include <algorithm>

namespace cad {

json CombineFeature::dataToJson() const {
    return json{{"target", target}, {"tools", tools}, {"operation", toString(operation)}, {"keepTools", keepTools}};
}

void CombineFeature::dataFromJson(const json &j) {
    target = jget<std::string>(j, "target", "");
    tools = jget<std::vector<std::string>>(j, "tools", {});
    operation = bodyOperationFromString(jget<std::string>(j, "operation", "join"));
    if(operation == BodyOperation::NewBody) operation = BodyOperation::Join;
    keepTools = jget<bool>(j, "keepTools", false);
}

FeatureResult CombineFeature::compute(const StatePtr &input, const ComputeContext &) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        const BodyId t = input->resolveBodyId(target);
        if(t.empty()) return {input, Status::error("the target body no longer exists")};
        std::vector<BodyId> ts;
        for(const auto &b : tools) {
            const BodyId real = input->resolveBodyId(b);
            if(real.empty()) {
                st.merge(Status::warning("tool body " + b + " no longer exists"));
                continue;
            }
            if(real != t && std::find(ts.begin(), ts.end(), real) == ts.end()) ts.push_back(real);
        }
        if(ts.empty()) return {input, Status::error("select at least one tool body")};

        const std::string prefix = "f" + std::to_string(id);
        const BoolOp op = operation == BodyOperation::Cut        ? BoolOp::Cut
                          : operation == BodyOperation::Intersect ? BoolOp::Common
                                                                  : BoolOp::Fuse;
        std::vector<const NamedShape *> toolShapes;
        for(const auto &b : ts) toolShapes.push_back(&input->body(b)->shape);
        std::shared_ptr<const Body> shown;
        if(operation == BodyOperation::Cut && !keepTools) {
            std::vector<TopoDS_Shape> v;
            for(const NamedShape *s : toolShapes) v.push_back(s->shape());
            shown = toolBody(NamedShape(makeCompound(v), {}));
        }
        BooleanResult br = runBoolean(op, {&input->body(t)->shape}, toolShapes, prefix);
        if(!br.ok) return {input, Status::error(br.error), shown};

        auto out = std::make_shared<ModelState>(*input);
        if(solidsOf(br.shape.shape()).empty()) {
            out->bodies.erase(t);
            st.merge(Status::warning("the target body was removed entirely"));
        } else {
            if(!validateResult(br.shape, prefix, st)) return {input, st, shown};
            replaceBody(*out, t, br.shape);
        }
        if(!keepTools) {
            for(const auto &b : ts) {
                out->bodies.erase(b);
                if(operation == BodyOperation::Join) out->mergedInto[b] = t;
            }
        }
        return {out, st, shown};
    });
}

} // namespace cad
