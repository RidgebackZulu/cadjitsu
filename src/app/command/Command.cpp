#include "command/Command.h"

#include "model/ModelView.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"
#include "viewport/ViewportTool.h"

#include <QPushButton>
#include <QTimer>

namespace cadly {

Command::Command(const CommandContext &ctx, cad::FeatureId editing) : m_ctx(ctx), m_editing(editing) {}

ValueField::Evaluator Command::evaluator() const {
    cad::Document *doc = m_ctx.doc;
    return [doc](const std::string &expr, cad::ValueKind kind) { return doc->params().evaluateExpression(expr, kind); };
}

// Never computes on the UI thread: until the background evaluation has
// produced it (the preview includes it), there is no base state yet.
cad::StatePtr Command::baseState() const {
    const int index = isEditing() ? m_ctx.doc->indexOf(m_editing) : m_ctx.doc->marker();
    return m_ctx.doc->knownStateAt(index);
}

// ---------------------------------------------------------------------------

CommandController::CommandController(const CommandContext &ctx, RecomputeService *recompute, QObject *parent)
    : QObject(parent), m_ctx(ctx), m_recompute(recompute) {
    connect(ctx.panel, &CommandPanel::accepted, this, [this] { commit(); });
    connect(ctx.panel, &CommandPanel::cancelled, this, &CommandController::cancel);
    connect(ctx.view, &ModelView::picked, this,
            [this](const std::optional<SelectionItem> &it, const PickHit &hit, Qt::KeyboardModifiers m) {
                if(m_cmd) m_cmd->picked(it, hit, m);
            });
    connect(ctx.view, &ModelView::markClicked, this, [this](int tag) {
        if(m_cmd) m_cmd->markClicked(tag);
    });
}

CommandController::~CommandController() {
    if(m_cmd && m_ctx.viewport->tool() == m_cmd->tool()) m_ctx.viewport->setTool(nullptr);
}

void CommandController::start(std::unique_ptr<Command> cmd) {
    if(m_cmd) cancel();
    m_cmd = std::move(cmd);
    m_ctx.panel->begin(m_cmd->title(), m_cmd->iconId());
    m_ctx.view->setCommandInput(true);
    m_cmd->setup();
    connect(m_cmd.get(), &Command::inputsChanged, this, [this] {
        // Coalesce bursts of changes (typing, dragging) into one preview.
        if(m_previewPending) return;
        m_previewPending = true;
        QTimer::singleShot(0, this, [this] {
            m_previewPending = false;
            preview();
        });
    });
    if(ViewportTool *t = m_cmd->tool()) m_ctx.viewport->setTool(t);
    emit activeChanged(true);
    emit editingChanged(m_cmd->editing());
    preview();
}

void CommandController::preview() {
    if(!m_cmd) return;
    QString why;
    std::shared_ptr<cad::Feature> f = m_cmd->build(why);
    cad::Document &doc = *m_ctx.doc;
    std::vector<cad::FeaturePtr> features = doc.features();
    int marker = doc.marker();
    m_candidateIndex = -1;
    if(f) {
        if(m_cmd->isEditing()) {
            const int i = doc.indexOf(m_cmd->editing());
            features[size_t(i)] = f;
            marker = i + 1; // rolled back to the edited feature, like Fusion
            m_candidateIndex = i;
        } else {
            f->id = doc.nextFeatureId();
            if(f->name.empty()) f->name = doc.defaultName(f->type());
            features.insert(features.begin() + marker, f);
            m_candidateIndex = marker;
            ++marker;
        }
    } else if(m_cmd->isEditing()) {
        marker = doc.indexOf(m_cmd->editing()); // without the feature
    }
    m_ctx.panel->setOkEnabled(f != nullptr);
    m_ctx.panel->setMessage(why, why.isEmpty() ? cad::Severity::Ok : cad::Severity::Warning);
    m_ctx.view->setForcedSketches(m_cmd->sketchesToShow());
    m_request = m_recompute->request(std::move(features), nullptr, marker, true);
}

bool CommandController::accepts(const EvaluationPtr &e) const {
    if(!m_cmd) return !e->preview;
    return e->preview && e->id >= m_request;
}

void CommandController::onEvaluation(const EvaluationPtr &e) {
    if(!m_cmd || !e->preview || e->id < m_request) return;
    if(m_candidateIndex >= 0 && m_candidateIndex < int(e->statuses.size())) {
        // A feature that fails to compute cannot be committed; its message says why.
        const cad::Status &st = e->statuses[size_t(m_candidateIndex)];
        m_ctx.panel->setMessage(QString::fromStdString(st.message), st.severity);
        m_ctx.panel->setOkEnabled(!st.isError());
    }
    m_cmd->previewed(e->state);
}

bool CommandController::commit() {
    if(!m_cmd || !m_ctx.panel->okButton()->isEnabled()) return false; // not ready, or its preview failed
    QString why;
    std::shared_ptr<cad::Feature> f = m_cmd->build(why);
    if(!f) {
        m_ctx.panel->setMessage(why.isEmpty() ? tr("The command is not ready.") : why, cad::Severity::Warning);
        return false;
    }
    // The document changes while the command is still active, so the model
    // requested when it ends is the committed one (no stale frame in between).
    cad::Document &doc = *m_ctx.doc;
    cad::FeatureId id;
    if(m_cmd->isEditing()) {
        id = f->id;
        doc.replaceFeature(f);
    } else {
        f->id = cad::kNoFeature;
        id = doc.addFeature(f);
    }
    finish();
    emit committed(id);
    return true;
}

void CommandController::cancel() {
    if(!m_cmd) return;
    finish();
}

void CommandController::finish() {
    if(m_cmd && m_ctx.viewport->tool() == m_cmd->tool()) m_ctx.viewport->setTool(nullptr);
    m_ctx.panel->end();
    m_ctx.view->setCommandInput(false);
    m_ctx.view->setFilter(SelectFilter::idle());
    m_ctx.view->setOriginForced(false);
    m_ctx.view->setForcedSketches({});
    m_ctx.view->clearSelection();
    // Delete the command from the event loop (this may run inside its handlers).
    if(m_cmd) m_cmd.release()->deleteLater();
    m_candidateIndex = -1;
    emit editingChanged(cad::kNoFeature);
    emit activeChanged(false);
}

} // namespace cadly
