#pragma once

#include "doc/Feature.h"
#include "doc/ResultCache.h"
#include "expr/ParamTable.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace cad {

// Evaluated states for a prefix of a timeline.
struct TimelineEvaluation {
    std::vector<uint64_t> keys;     // cache key after each feature (all features)
    std::vector<StatePtr> states;   // state after features[0..i] (computed prefix only)
    std::vector<Status> statuses;   // status of each computed feature
};

// Computes timeline states [computed, upTo) on top of `eval`, using and
// filling `cache`. Returns the number of features actually computed (misses).
// Safe to call from a worker thread with immutable inputs.
int evaluateTimeline(const std::vector<FeaturePtr> &features, const ParamTable &params, int upTo,
                     TimelineEvaluation &eval, ResultCache &cache, const std::atomic<bool> *cancel = nullptr);

// Cache keys for every feature of a timeline given evaluated parameters.
std::vector<uint64_t> timelineKeys(const std::vector<FeaturePtr> &features, const ParamTable &params);

// Builds and evaluates the parameter table of a timeline.
std::shared_ptr<ParamTable> buildParamTable(const std::vector<FeaturePtr> &features);

// A Cadly document: the feature timeline with its history marker, body display
// settings, undo/redo, and (lazily computed, cached) model states.
class Document {
public:
    Document();

    // --- Timeline -----------------------------------------------------------
    const std::vector<FeaturePtr> &features() const { return m_features; }
    int marker() const { return m_marker; } // number of active (not rolled back) features
    FeaturePtr feature(FeatureId id) const;
    int indexOf(FeatureId id) const;
    // Features that use `id` (directly), e.g. extrudes of a sketch.
    std::vector<FeatureId> dependents(FeatureId id) const;

    // --- Edits (each is one undo step) ----------------------------------------
    // Inserts at the marker (assigning id and default name) and advances the marker.
    FeatureId addFeature(std::shared_ptr<Feature> f, const std::string &undoLabel = {});
    // `recordUndo` = false folds the change into the previous undo step (a new
    // sketch is created and later filled in as one step).
    bool replaceFeature(std::shared_ptr<Feature> f, const std::string &undoLabel = {}, bool recordUndo = true);
    bool setSuppressed(FeatureId id, bool suppressed);
    bool deleteFeature(FeatureId id);
    bool renameFeature(FeatureId id, const std::string &name);
    // Moves the history marker; `recordUndo` = false while the user is dragging it.
    void setMarker(int index, bool recordUndo = true);

    void renameBody(const BodyId &id, const std::string &name);
    std::string bodyName(const Body &body) const;
    void setBodyVisible(const BodyId &id, bool visible);
    bool bodyVisible(const BodyId &id) const;

    // --- Parameters -------------------------------------------------------------
    std::string allocateParamName();
    ParamSlot makeSlot(const std::string &expr) { return {allocateParamName(), expr}; }
    const ParamTable &params();
    std::shared_ptr<const ParamTable> paramTable();

    // --- Evaluation -------------------------------------------------------------
    StatePtr stateAt(int index); // after the first `index` features (0 = empty model)
    StatePtr displayedState() { return stateAt(m_marker); }
    Status statusOf(FeatureId id);
    uint64_t keyAt(int index); // cache key of the state after `index` features
    size_t computeCount() const { return m_computeCount; }
    ResultCache &cache() { return *m_cache; }
    const std::shared_ptr<ResultCache> &sharedCache() const { return m_cache; }

    // --- Undo / redo --------------------------------------------------------------
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    std::string undoLabel() const { return m_undo.empty() ? std::string() : m_undo.back().label; }
    std::string redoLabel() const { return m_redo.empty() ? std::string() : m_redo.back().label; }
    bool undo();
    bool redo();
    // Records an undo step for an edit made outside the methods above.
    void pushUndo(const std::string &label);

    // --- Files --------------------------------------------------------------------
    json toJson() const;
    bool fromJson(const json &j, std::string &error); // replaces content, clears undo
    bool save(const std::string &path, std::string &error) const;
    bool load(const std::string &path, std::string &error);
    void clear(); // new empty document

    // Incremented on every change; `changed` is invoked after each change.
    uint64_t revision() const { return m_revision; }
    std::function<void()> changed;

private:
    struct UndoEntry {
        std::string label;
        json snapshot;
    };

    json snapshot() const;
    void restore(const json &snap);
    void touch(bool structural);
    std::string defaultName(FeatureType type) const;
    void ensureParams();
    void ensureKeys();

    std::vector<FeaturePtr> m_features;
    int m_marker = 0;
    FeatureId m_nextId = 1;
    int m_nextParam = 1;
    std::map<BodyId, std::string> m_bodyNames;
    std::set<BodyId> m_hiddenBodies;

    std::vector<UndoEntry> m_undo, m_redo;
    uint64_t m_revision = 0;

    std::shared_ptr<ResultCache> m_cache;
    std::shared_ptr<ParamTable> m_params;
    TimelineEvaluation m_eval;
    bool m_keysValid = false;
    size_t m_computeCount = 0;
};

} // namespace cad
