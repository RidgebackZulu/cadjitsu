#include "doc/Feature.h"

#include "features/ChamferFeature.h"
#include "features/CombineFeature.h"
#include "features/ConstructionPlaneFeature.h"
#include "features/ExtrudeFeature.h"
#include "features/FilletFeature.h"
#include "features/HoleFeature.h"
#include "features/SketchFeature.h"

#include <Standard_Failure.hxx>

#include <exception>

namespace cad {

namespace {

struct TypeName {
    FeatureType type;
    const char *id;
    const char *stem;
};

const TypeName kTypes[] = {
    {FeatureType::Sketch, "sketch", "Sketch"},
    {FeatureType::Extrude, "extrude", "Extrude"},
    {FeatureType::Fillet, "fillet", "Fillet"},
    {FeatureType::Chamfer, "chamfer", "Chamfer"},
    {FeatureType::Hole, "hole", "Hole"},
    {FeatureType::Combine, "combine", "Combine"},
    {FeatureType::ConstructionPlane, "plane", "Plane"},
};

} // namespace

const char *toString(FeatureType t) {
    for(const auto &e : kTypes)
        if(e.type == t) return e.id;
    return "sketch";
}

bool featureTypeFromString(const std::string &s, FeatureType &out) {
    for(const auto &e : kTypes) {
        if(s == e.id) {
            out = e.type;
            return true;
        }
    }
    return false;
}

const char *displayStem(FeatureType t) {
    for(const auto &e : kTypes)
        if(e.type == t) return e.stem;
    return "Feature";
}

bool ComputeContext::value(const ParamSlot &slot, double &out, Status &status, ValueKind kind) const {
    if(!params) {
        status = Status::error("internal: no parameter table");
        return false;
    }
    const ParamValue *v = params->find(slot.name);
    if(!v) {
        // Not registered (e.g. a preview of an unsaved feature): evaluate directly.
        const EvalResult r = params->evaluateExpression(slot.expr, kind);
        if(!r.ok) {
            status = Status::error(slot.expr + ": " + r.error);
            return false;
        }
        out = r.value;
        return true;
    }
    if(!v->ok) {
        status = Status::error(slot.name + " = " + v->def.expr + ": " + v->error);
        return false;
    }
    out = v->value;
    return true;
}

json Feature::toJson() const {
    json j{{"id", id}, {"type", cad::toString(type())}, {"name", name}, {"data", dataToJson()}};
    if(suppressed) j["suppressed"] = true;
    return j;
}

std::shared_ptr<Feature> Feature::create(FeatureType type) {
    switch(type) {
    case FeatureType::Sketch: return std::make_shared<SketchFeature>();
    case FeatureType::Extrude: return std::make_shared<ExtrudeFeature>();
    case FeatureType::Fillet: return std::make_shared<FilletFeature>();
    case FeatureType::Chamfer: return std::make_shared<ChamferFeature>();
    case FeatureType::Hole: return std::make_shared<HoleFeature>();
    case FeatureType::Combine: return std::make_shared<CombineFeature>();
    case FeatureType::ConstructionPlane: return std::make_shared<ConstructionPlaneFeature>();
    }
    return nullptr;
}

std::shared_ptr<Feature> Feature::fromJson(const json &j, std::string *error) {
    FeatureType type;
    if(!j.is_object() || !featureTypeFromString(jget<std::string>(j, "type", ""), type)) {
        if(error) *error = "unknown feature type";
        return nullptr;
    }
    auto f = create(type);
    f->id = jget<int>(j, "id", kNoFeature);
    f->name = jget<std::string>(j, "name", "");
    f->suppressed = jget<bool>(j, "suppressed", false);
    f->dataFromJson(j.value("data", json::object()));
    return f;
}

FeatureResult guardedCompute(const StatePtr &input, const std::function<FeatureResult()> &fn) {
    try {
        FeatureResult r = fn();
        if(!r.state) r.state = input;
        return r;
    } catch(const Standard_Failure &e) {
        const char *msg = e.GetMessageString();
        return {input, Status::error(std::string("geometry kernel error: ") +
                                     (msg && *msg ? msg : e.DynamicType()->Name()))};
    } catch(const std::exception &e) {
        return {input, Status::error(std::string("error: ") + e.what())};
    } catch(...) {
        return {input, Status::error("unexpected error")};
    }
}

} // namespace cad
