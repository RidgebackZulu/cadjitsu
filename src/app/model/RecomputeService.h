#pragma once

#include "doc/Document.h"

#include <QObject>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cadly {

// One evaluated timeline, ready to display: the document's own, or a
// command's preview with a candidate feature in it.
struct Evaluation {
    uint64_t id = 0;                       // request number
    std::vector<cad::FeaturePtr> features; // the timeline that was evaluated
    int marker = 0;                        // number of features applied
    cad::StatePtr state;                   // the model after `marker` features
    std::vector<cad::Status> statuses;     // one per applied feature
    bool preview = false;
    // Previews: what the last applied feature cuts away (drawn translucent).
    std::shared_ptr<const cad::Body> tool;
};

using EvaluationPtr = std::shared_ptr<const Evaluation>;

// Evaluates timelines on a background thread so modelling never blocks the
// UI. A new request cancels the one in progress (between features); only the
// latest result is delivered, on the UI thread. Results go through the
// document's content-addressed cache, so committing a previewed feature, undo
// and scrubbing are cache hits.
class RecomputeService : public QObject {
    Q_OBJECT

public:
    explicit RecomputeService(std::shared_ptr<cad::ResultCache> cache, QObject *parent = nullptr);
    ~RecomputeService() override;

    uint64_t request(std::vector<cad::FeaturePtr> features, std::shared_ptr<const cad::ParamTable> params, int marker,
                     bool preview = false);
    // Evaluates the document as it is now.
    uint64_t requestDocument(cad::Document &doc);
    bool busy() const { return m_busy; }
    EvaluationPtr latest() const { return m_latest; }
    // Features computed so far (cache hits and suppressed features excluded).
    size_t computedFeatures() const { return m_computed.load(); }
    // Spins the event loop until the latest request has been delivered.
    bool waitIdle(int timeoutMs = 60000);
    // The model was just shown some other way (evaluated synchronously): results
    // of the requests made so far are out of date and are not delivered.
    void supersedeRequests() { m_superseded = m_requested; }

signals:
    void finished(cadly::EvaluationPtr result);
    void busyChanged(bool busy);

private:
    struct Job {
        uint64_t id = 0;
        std::vector<cad::FeaturePtr> features;
        std::shared_ptr<const cad::ParamTable> params;
        int marker = 0;
        bool preview = false;
        std::atomic<bool> cancel{false};
    };

    void run();
    void deliver(EvaluationPtr result);
    void setBusy(bool busy);

    std::shared_ptr<cad::ResultCache> m_cache;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::shared_ptr<Job> m_pending, m_current;
    bool m_stop = false;
    std::atomic<size_t> m_computed{0};

    uint64_t m_requested = 0; // UI thread
    uint64_t m_superseded = 0; // UI thread: requests up to this one are not delivered
    EvaluationPtr m_latest;   // UI thread
    bool m_busy = false;      // UI thread
};

} // namespace cadly

Q_DECLARE_METATYPE(cadly::EvaluationPtr)
