#include "doc/Document.h"

#include "base/Hash.h"
#include "base/Version.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace cad {

namespace {

constexpr int kFormatVersion = 1;
constexpr size_t kMaxUndo = 200;

StatePtr emptyState() {
    static const StatePtr empty = std::make_shared<ModelState>();
    return empty;
}

} // namespace

// ---------------------------------------------------------------------------
// Timeline evaluation

std::shared_ptr<ParamTable> buildParamTable(const std::vector<FeaturePtr> &features) {
    auto table = std::make_shared<ParamTable>();
    for(const auto &f : features)
        for(const auto &p : f->params()) table->add(p);
    table->evaluateAll();
    return table;
}

std::vector<uint64_t> timelineKeys(const std::vector<FeaturePtr> &features, const ParamTable &params) {
    std::vector<uint64_t> keys;
    keys.reserve(features.size());
    uint64_t k = hashString("cadly-timeline-v1");
    for(const auto &f : features) {
        uint64_t h = hashString(f->dataToJson().dump());
        h = hashCombine(h, hashString(toString(f->type())));
        h = hashCombine(h, uint64_t(f->id));
        h = hashCombine(h, f->suppressed ? 1u : 2u);
        for(const auto &p : f->params()) {
            const ParamValue *v = params.find(p.name);
            h = hashCombine(h, hashString(p.name));
            h = hashCombine(h, v && v->ok ? hashDouble(v->value) : hashString(v ? v->error : "missing"));
        }
        k = hashCombine(k, h);
        keys.push_back(k);
    }
    return keys;
}

int evaluateTimeline(const std::vector<FeaturePtr> &features, const ParamTable &params, int upTo,
                     TimelineEvaluation &eval, ResultCache &cache, const std::atomic<bool> *cancel) {
    if(eval.keys.size() != features.size()) eval.keys = timelineKeys(features, params);
    upTo = std::clamp(upTo, 0, int(features.size()));
    int computed = 0;
    StatePtr prev = eval.states.empty() ? emptyState() : eval.states.back();
    for(size_t i = eval.states.size(); i < size_t(upTo); ++i) {
        if(cancel && cancel->load()) break;
        const Feature &f = *features[i];
        if(f.suppressed) {
            eval.states.push_back(prev);
            eval.statuses.push_back(Status::ok());
            continue;
        }
        if(auto hit = cache.find(eval.keys[i])) {
            eval.states.push_back(hit->state);
            eval.statuses.push_back(hit->status);
            prev = hit->state;
            continue;
        }
        ComputeContext ctx;
        ctx.params = &params;
        ctx.cancel = cancel;
        FeatureResult r = f.compute(prev, ctx);
        if(cancel && cancel->load()) break; // a cancelled result may be incomplete
        if(!r.state) r.state = prev;
        cache.insert(eval.keys[i], {r.state, r.status});
        eval.states.push_back(r.state);
        eval.statuses.push_back(r.status);
        prev = r.state;
        ++computed;
    }
    return computed;
}

// ---------------------------------------------------------------------------

Document::Document() : m_cache(std::make_shared<ResultCache>()) {}

FeaturePtr Document::feature(FeatureId id) const {
    for(const auto &f : m_features)
        if(f->id == id) return f;
    return nullptr;
}

int Document::indexOf(FeatureId id) const {
    for(size_t i = 0; i < m_features.size(); ++i)
        if(m_features[i]->id == id) return int(i);
    return -1;
}

std::vector<FeatureId> Document::dependents(FeatureId id) const {
    std::vector<FeatureId> out;
    for(const auto &f : m_features) {
        const auto deps = f->dependencies();
        if(std::find(deps.begin(), deps.end(), id) != deps.end()) out.push_back(f->id);
    }
    return out;
}

std::string Document::defaultName(FeatureType type) const {
    const std::string stem = displayStem(type);
    int maxN = 0;
    for(const auto &f : m_features) {
        if(f->name.rfind(stem, 0) != 0) continue;
        const std::string rest = f->name.substr(stem.size());
        if(!rest.empty() && std::all_of(rest.begin(), rest.end(), ::isdigit)) maxN = std::max(maxN, std::stoi(rest));
    }
    return stem + std::to_string(maxN + 1);
}

FeatureId Document::addFeature(std::shared_ptr<Feature> f, const std::string &undoLabel) {
    if(!f) return kNoFeature;
    f->id = m_nextId++;
    if(f->name.empty()) f->name = defaultName(f->type());
    pushUndo(undoLabel.empty() ? "Create " + f->name : undoLabel);
    m_features.insert(m_features.begin() + m_marker, f);
    ++m_marker;
    touch(true);
    return f->id;
}

bool Document::replaceFeature(std::shared_ptr<Feature> f, const std::string &undoLabel) {
    const int i = f ? indexOf(f->id) : -1;
    if(i < 0) return false;
    pushUndo(undoLabel.empty() ? "Edit " + f->name : undoLabel);
    m_features[size_t(i)] = f;
    touch(true);
    return true;
}

bool Document::setSuppressed(FeatureId id, bool suppressed) {
    const int i = indexOf(id);
    if(i < 0 || m_features[size_t(i)]->suppressed == suppressed) return false;
    auto copy = m_features[size_t(i)]->clone();
    copy->suppressed = suppressed;
    pushUndo((suppressed ? "Suppress " : "Unsuppress ") + copy->name);
    m_features[size_t(i)] = copy;
    touch(true);
    return true;
}

bool Document::deleteFeature(FeatureId id) {
    const int i = indexOf(id);
    if(i < 0) return false;
    pushUndo("Delete " + m_features[size_t(i)]->name);
    m_features.erase(m_features.begin() + i);
    if(m_marker > i) --m_marker;
    touch(true);
    return true;
}

bool Document::renameFeature(FeatureId id, const std::string &name) {
    const int i = indexOf(id);
    if(i < 0 || name.empty() || m_features[size_t(i)]->name == name) return false;
    auto copy = m_features[size_t(i)]->clone();
    pushUndo("Rename " + copy->name);
    copy->name = name;
    m_features[size_t(i)] = copy;
    touch(false);
    return true;
}

void Document::setMarker(int index, bool recordUndo) {
    index = std::clamp(index, 0, int(m_features.size()));
    if(index == m_marker) return;
    if(recordUndo) pushUndo("Move History Marker");
    m_marker = index;
    touch(false);
}

void Document::renameBody(const BodyId &id, const std::string &name) {
    pushUndo("Rename Body");
    if(name.empty()) m_bodyNames.erase(id);
    else m_bodyNames[id] = name;
    touch(false);
}

std::string Document::bodyName(const Body &body) const {
    auto it = m_bodyNames.find(body.id);
    return it == m_bodyNames.end() ? body.name : it->second;
}

void Document::setBodyVisible(const BodyId &id, bool visible) {
    if(visible == bodyVisible(id)) return;
    if(visible) m_hiddenBodies.erase(id);
    else m_hiddenBodies.insert(id);
    touch(false);
}

bool Document::bodyVisible(const BodyId &id) const { return !m_hiddenBodies.count(id); }

std::string Document::allocateParamName() {
    std::set<std::string> used;
    for(const auto &f : m_features)
        for(const auto &p : f->params()) used.insert(p.name);
    for(;;) {
        std::string n = "d" + std::to_string(m_nextParam++);
        if(!used.count(n)) return n;
    }
}

void Document::ensureParams() {
    if(!m_params) m_params = buildParamTable(m_features);
}

void Document::ensureKeys() {
    ensureParams();
    if(m_keysValid) return;
    m_eval = TimelineEvaluation();
    m_eval.keys = timelineKeys(m_features, *m_params);
    m_cache->setPinned(std::set<uint64_t>(m_eval.keys.begin(), m_eval.keys.end()));
    m_keysValid = true;
}

const ParamTable &Document::params() {
    ensureParams();
    return *m_params;
}

std::shared_ptr<const ParamTable> Document::paramTable() {
    ensureParams();
    return m_params;
}

StatePtr Document::stateAt(int index) {
    ensureKeys();
    index = std::clamp(index, 0, int(m_features.size()));
    if(index == 0) return emptyState();
    if(int(m_eval.states.size()) < index)
        m_computeCount += size_t(evaluateTimeline(m_features, *m_params, index, m_eval, *m_cache));
    return m_eval.states[size_t(index) - 1];
}

uint64_t Document::keyAt(int index) {
    ensureKeys();
    if(index <= 0 || index > int(m_eval.keys.size())) return 0;
    return m_eval.keys[size_t(index) - 1];
}

Status Document::statusOf(FeatureId id) {
    const int i = indexOf(id);
    if(i < 0 || i >= m_marker) return Status::ok();
    stateAt(i + 1);
    return m_eval.statuses[size_t(i)];
}

void Document::touch(bool structural) {
    if(structural) {
        m_params.reset();
        m_keysValid = false;
    }
    ++m_revision;
    if(changed) changed();
}

// ---------------------------------------------------------------------------
// Undo

json Document::snapshot() const {
    json features = json::array();
    for(const auto &f : m_features) features.push_back(f->toJson());
    json names = json::object();
    for(const auto &[id, n] : m_bodyNames) names[id] = n;
    return json{{"features", features},
                {"marker", m_marker},
                {"nextId", m_nextId},
                {"nextParam", m_nextParam},
                {"bodyNames", names},
                {"hiddenBodies", std::vector<std::string>(m_hiddenBodies.begin(), m_hiddenBodies.end())}};
}

void Document::restore(const json &snap) {
    m_features.clear();
    for(const auto &jf : snap.value("features", json::array())) {
        std::string err;
        if(auto f = Feature::fromJson(jf, &err)) m_features.push_back(f);
    }
    m_marker = std::clamp(jget<int>(snap, "marker", int(m_features.size())), 0, int(m_features.size()));
    m_nextId = jget<int>(snap, "nextId", 1);
    for(const auto &f : m_features) m_nextId = std::max(m_nextId, f->id + 1);
    m_nextParam = jget<int>(snap, "nextParam", 1);
    m_bodyNames.clear();
    const json names = snap.value("bodyNames", json::object());
    for(auto it = names.begin(); it != names.end(); ++it)
        if(it.value().is_string()) m_bodyNames[it.key()] = it.value().get<std::string>();
    const auto hidden = jget<std::vector<std::string>>(snap, "hiddenBodies", {});
    m_hiddenBodies = std::set<BodyId>(hidden.begin(), hidden.end());
    touch(true);
}

void Document::pushUndo(const std::string &label) {
    m_undo.push_back({label, snapshot()});
    if(m_undo.size() > kMaxUndo) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

bool Document::undo() {
    if(m_undo.empty()) return false;
    UndoEntry e = std::move(m_undo.back());
    m_undo.pop_back();
    m_redo.push_back({e.label, snapshot()});
    restore(e.snapshot);
    return true;
}

bool Document::redo() {
    if(m_redo.empty()) return false;
    UndoEntry e = std::move(m_redo.back());
    m_redo.pop_back();
    m_undo.push_back({e.label, snapshot()});
    restore(e.snapshot);
    return true;
}

// ---------------------------------------------------------------------------
// Files

json Document::toJson() const {
    json j = snapshot();
    j["format"] = "cadly";
    j["version"] = kFormatVersion;
    j["generator"] = std::string("Cadly ") + version();
    j["units"] = "mm";
    return j;
}

bool Document::fromJson(const json &j, std::string &error) {
    if(!j.is_object() || jget<std::string>(j, "format", "") != "cadly") {
        error = "not a Cadly document";
        return false;
    }
    if(jget<int>(j, "version", 0) > kFormatVersion) {
        error = "this document was made by a newer version of Cadly";
        return false;
    }
    restore(j);
    m_undo.clear();
    m_redo.clear();
    return true;
}

bool Document::save(const std::string &path, std::string &error) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out) {
        error = "cannot write " + path;
        return false;
    }
    out << toJson().dump(2) << "\n";
    if(!out) {
        error = "failed writing " + path;
        return false;
    }
    return true;
}

bool Document::load(const std::string &path, std::string &error) {
    std::ifstream in(path, std::ios::binary);
    if(!in) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    if(j.is_discarded()) {
        error = "the file is not valid JSON";
        return false;
    }
    return fromJson(j, error);
}

void Document::clear() {
    m_features.clear();
    m_marker = 0;
    m_nextId = 1;
    m_nextParam = 1;
    m_bodyNames.clear();
    m_hiddenBodies.clear();
    m_undo.clear();
    m_redo.clear();
    touch(true);
}

} // namespace cad
