#include "expr/ParamTable.h"

#include <functional>

namespace cad {

namespace {

Dim dimFor(ValueKind k) {
    switch(k) {
    case ValueKind::Length: return Dim::len();
    case ValueKind::Angle: return Dim::ang();
    case ValueKind::Scalar: return Dim::scalar();
    }
    return Dim::scalar();
}

} // namespace

void ParamTable::clear() {
    m_values.clear();
    m_order.clear();
    m_duplicates.clear();
}

bool ParamTable::add(const ParamDef &def) {
    if(m_values.count(def.name)) {
        m_duplicates.push_back(def.name);
        return false;
    }
    ParamValue v;
    v.def = def;
    m_values.emplace(def.name, std::move(v));
    m_order.push_back(def.name);
    return true;
}

void ParamTable::evaluateAll() {
    enum class Mark { None, Visiting, Done };
    std::map<std::string, Mark> marks;

    for(auto &[name, v] : m_values) {
        v.ok = false;
        v.error.clear();
        v.deps.clear();
        if(!isValidParamName(name)) v.error = "invalid parameter name '" + name + "'";
        else if(!referencedNames(v.def.expr, v.deps)) {
            std::string err;
            Quantity q;
            evaluateQuantity(v.def.expr, {}, q, err);
            v.error = err.empty() ? "invalid expression" : err;
        }
    }
    for(const auto &dup : m_duplicates) {
        auto it = m_values.find(dup);
        if(it != m_values.end() && it->second.error.empty())
            it->second.error = "parameter name '" + dup + "' is used twice";
    }

    std::function<void(const std::string &)> visit = [&](const std::string &name) {
        ParamValue &v = m_values.at(name);
        Mark &m = marks[name];
        if(m == Mark::Done) return;
        if(m == Mark::Visiting) return; // cycle detected by the caller
        m = Mark::Visiting;

        if(v.error.empty()) {
            for(const auto &dep : v.deps) {
                auto it = m_values.find(dep);
                if(it == m_values.end()) {
                    v.error = "unknown parameter '" + dep + "'";
                    break;
                }
                if(marks[dep] == Mark::Visiting) {
                    v.error = "circular reference through '" + dep + "'";
                    break;
                }
                visit(dep);
                if(!it->second.ok) {
                    v.error = "'" + dep + "' has an error";
                    break;
                }
            }
        }
        if(v.error.empty()) {
            const EvalResult r = evaluate(v.def.expr, v.def.kind, [&](const std::string &n) -> std::optional<Quantity> {
                auto it = m_values.find(n);
                if(it == m_values.end() || !it->second.ok) return std::nullopt;
                return Quantity{it->second.value, dimFor(it->second.def.kind)};
            });
            if(r.ok) {
                v.ok = true;
                v.value = r.value;
            } else {
                v.error = r.error;
            }
        }
        m = Mark::Done;
    };

    for(const auto &name : m_order) visit(name);

    // Every member of a cycle is an error, not only the one that closed it.
    bool changed = true;
    while(changed) {
        changed = false;
        for(auto &[name, v] : m_values) {
            if(!v.ok) continue;
            for(const auto &dep : v.deps) {
                auto it = m_values.find(dep);
                if(it == m_values.end() || !it->second.ok) {
                    v.ok = false;
                    v.error = "'" + dep + "' has an error";
                    changed = true;
                    break;
                }
            }
        }
    }
}

const ParamValue *ParamTable::find(const std::string &name) const {
    auto it = m_values.find(name);
    return it == m_values.end() ? nullptr : &it->second;
}

EvalResult ParamTable::evaluateExpression(const std::string &expr, ValueKind kind) const {
    return evaluate(expr, kind, [&](const std::string &n) -> std::optional<Quantity> {
        auto it = m_values.find(n);
        if(it == m_values.end() || !it->second.ok) return std::nullopt;
        return Quantity{it->second.value, dimFor(it->second.def.kind)};
    });
}

std::string ParamTable::nextFreeName() const {
    for(int i = 1;; ++i) {
        std::string n = "d" + std::to_string(i);
        if(!m_values.count(n)) return n;
    }
}

} // namespace cad
