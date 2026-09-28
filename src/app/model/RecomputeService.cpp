#include "model/RecomputeService.h"

#include "mesh/MeshData.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>

#include <algorithm>

namespace cadjitsu {

RecomputeService::RecomputeService(std::shared_ptr<cad::ResultCache> cache, QObject *parent)
    : QObject(parent), m_cache(std::move(cache)) {
    qRegisterMetaType<EvaluationPtr>();
    m_thread.reset(QThread::create([this] { run(); }));
    // Booleans and fillets recurse deeply; macOS gives secondary threads only
    // 512 KB of stack by default.
    m_thread->setStackSize(16u * 1024u * 1024u);
    m_thread->start();
}

RecomputeService::~RecomputeService() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop = true;
        if(m_current) m_current->cancel = true;
        m_pending.reset();
    }
    m_cv.notify_all();
    if(m_thread) m_thread->wait();
}

uint64_t RecomputeService::request(std::vector<cad::FeaturePtr> features, std::shared_ptr<const cad::ParamTable> params,
                                   int marker, bool preview, bool live) {
    auto job = std::make_shared<Job>();
    job->id = ++m_requested;
    job->features = std::move(features);
    job->params = params ? std::move(params) : cad::buildParamTable(job->features);
    job->marker = std::clamp(marker, 0, int(job->features.size()));
    job->preview = preview || live;
    job->live = live;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if(m_current) m_current->cancel = true;
        m_pending = job;
    }
    m_cv.notify_all();
    setBusy(true);
    return job->id;
}

uint64_t RecomputeService::requestDocument(cad::Document &doc) {
    return request(doc.features(), doc.paramTable(), doc.marker(), false);
}

void RecomputeService::run() {
    for(;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [&] { return m_stop || m_pending; });
            if(m_stop) return;
            job = std::move(m_pending);
            m_current = job;
        }
        cad::TimelineEvaluation eval;
        eval.keys = cad::timelineKeys(job->features, *job->params);
        m_computed += size_t(cad::evaluateTimeline(job->features, *job->params, job->marker, eval, *m_cache, &job->cancel));
        if(job->cancel || int(eval.states.size()) < job->marker) continue;

        auto result = std::make_shared<Evaluation>();
        result->id = job->id;
        result->features = job->features;
        result->marker = job->marker;
        result->state = job->marker > 0 ? eval.states[size_t(job->marker) - 1] : std::make_shared<const cad::ModelState>();
        result->statuses = eval.statuses;
        result->preview = job->preview;
        result->live = job->live;
        if(job->preview && !job->live && job->marker > 0) result->tool = eval.tools[size_t(job->marker) - 1];
        // Tessellate what will be drawn, so the UI thread never waits for it.
        for(const auto &kv : result->state->bodies) {
            if(job->cancel) break;
            kv.second->mesh();
        }
        if(result->tool && !job->cancel) result->tool->mesh();
        // A preview's command draws its inputs on the model before the
        // previewed feature: tessellate that too.
        if(job->preview && job->marker >= 2)
            for(const auto &kv : eval.states[size_t(job->marker) - 2]->bodies) {
                if(job->cancel) break;
                kv.second->mesh();
            }
        if(job->cancel) continue;
        QMetaObject::invokeMethod(this, [this, result] { deliver(result); }, Qt::QueuedConnection);

        // Carry on to the end of the timeline so scrubbing forward is instant.
        if(!job->preview && job->marker < int(job->features.size()))
            m_computed += size_t(cad::evaluateTimeline(job->features, *job->params, int(job->features.size()), eval,
                                                       *m_cache, &job->cancel));
    }
}

void RecomputeService::deliver(EvaluationPtr result) {
    if(result->id == m_requested) setBusy(false);
    if(result->id <= m_superseded) return;
    if(m_latest && result->id < m_latest->id) return;
    m_latest = result;
    emit finished(result);
}

void RecomputeService::setBusy(bool busy) {
    if(busy == m_busy) return;
    m_busy = busy;
    emit busyChanged(busy);
}

bool RecomputeService::waitIdle(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while(m_busy && t.elapsed() < timeoutMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return !m_busy;
}

} // namespace cadjitsu
