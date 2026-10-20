#include "features/RemoveFeature.h"

namespace cad {

json RemoveFeature::dataToJson() const { return json{{"bodies", bodies}}; }

void RemoveFeature::dataFromJson(const json &j) { bodies = jget<std::vector<std::string>>(j, "bodies", {}); }

FeatureResult RemoveFeature::compute(const StatePtr &input, const ComputeContext &) const {
    if(bodies.empty()) return {input, Status::error("pick the bodies to delete")};
    auto out = std::make_shared<ModelState>(*input);
    std::vector<BodyId> gone;
    int removed = 0;
    for(const BodyId &b : bodies) {
        if(out->bodies.erase(b)) ++removed;
        else gone.push_back(b);
    }
    Status st;
    if(!removed) return {input, Status::error("the bodies to delete are already gone")};
    if(!gone.empty()) {
        std::string list;
        for(const BodyId &b : gone) list += (list.empty() ? "" : ", ") + b;
        st.merge(Status::warning(list + (gone.size() == 1 ? " is" : " are") + " already gone"));
    }
    return {out, st};
}

} // namespace cad
