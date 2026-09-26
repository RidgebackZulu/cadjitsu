#pragma once

#include "base/Ids.h"
#include "expr/Expression.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace cad {

// A named numeric input owned by a feature: an extrude distance, a fillet
// radius, a sketch dimension... Like Fusion 360 each one gets a model
// parameter name ("d7") that other expressions may reference.
struct ParamDef {
    std::string name;
    std::string expr;
    ValueKind kind = ValueKind::Length;
    FeatureId owner = kNoFeature;
    std::string label; // human readable, e.g. "Extrude1 Distance"
};

struct ParamValue {
    ParamDef def;
    bool ok = false;
    double value = 0.0; // base units (mm / rad)
    std::string error;
    std::set<std::string> deps;
};

// Evaluates every parameter of a document in dependency order, detecting
// unknown names and circular references.
class ParamTable {
public:
    void clear();

    // Returns false (and records an error on the existing entry) if the name is taken.
    bool add(const ParamDef &def);

    void evaluateAll();

    const ParamValue *find(const std::string &name) const;
    bool contains(const std::string &name) const { return find(name) != nullptr; }
    const std::vector<std::string> &order() const { return m_order; }
    size_t size() const { return m_values.size(); }

    // Evaluates an arbitrary expression against the current table.
    EvalResult evaluateExpression(const std::string &expr, ValueKind kind) const;

    // Lowest "dN" name not yet used.
    std::string nextFreeName() const;

private:
    std::map<std::string, ParamValue> m_values;
    std::vector<std::string> m_order;
    std::vector<std::string> m_duplicates;
};

} // namespace cad
