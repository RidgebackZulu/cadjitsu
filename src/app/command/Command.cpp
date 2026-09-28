#include "command/Command.h"

#include "command/CanvasValueBox.h"
#include "model/ModelView.h"
#include "ui/Icons.h"
#include "viewport/Viewport.h"
#include "viewport/ViewportTool.h"

#include <QKeyEvent>
#include <QPushButton>
#include <QTimer>

namespace cadjitsu {

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

ValueField *Command::canvasValue() const {
    for(ValueField *f : m_ctx.panel->findChildren<ValueField *>())
        if(f->isVisibleTo(m_ctx.panel) && f->isEnabled()) return f;
    return nullptr;
}

// ---------------------------------------------------------------------------

CommandController::CommandController(const CommandContext &ctx, RecomputeService *recompute, QObject *parent)
    : QObject(parent), m_ctx(ctx), m_recompute(recompute) {
    connect(ctx.panel, &CommandPanel::accepted, this, [this] { commit(); });
    connect(ctx.panel, &CommandPanel::cancelled, this, &CommandController::cancel);
    connect(ctx.view, &ModelView::picked, this,
            [this](const std::optional<SelectionItem> &it, const PickHit &hit, Qt::KeyboardModifiers m) {
                if(!m_cmd) return;
                if(hit.valid()) m_cmd->notePick(hit.point);
                m_cmd->picked(it, hit, m);
                placeCanvasBox();
            });
    m_box = new CanvasValueBox(ctx.viewport);
    connect(m_box, &CanvasValueBox::commitRequested, this, [this] { commit(); });
    connect(m_box, &CanvasValueBox::cancelRequested, this, &CommandController::cancel);
    // Follow the view (and the arrow) every frame.
    connect(ctx.viewport, &QRhiWidget::frameSubmitted, this, &CommandController::placeCanvasBox);
    ctx.viewport->installEventFilter(this);
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
        // OK takes a click straight away (commit() checks the inputs again).
        m_ctx.panel->setOkEnabled(true);
        // Coalesce bursts of changes (typing, dragging) into one preview.
        if(m_previewPending) return;
        m_previewPending = true;
        QTimer::singleShot(0, this, [this] {
            m_previewPending = false;
            preview();
            placeCanvasBox();
        });
    });
    if(ViewportTool *t = m_cmd->tool()) m_ctx.viewport->setTool(t);
    emit activeChanged(true);
    emit editingChanged(m_cmd->editing());
    preview();
    placeCanvasBox();
}

void CommandController::placeCanvasBox() {
    ValueField *f = m_cmd ? m_cmd->canvasValue() : nullptr;
    const auto anchor = m_cmd ? m_cmd->canvasAnchor() : std::nullopt;
    m_box->bind(f);
    m_box->setAccent(m_cmd ? m_cmd->canvasAccent() : QColor());
    if(!f || !anchor) {
        m_box->hide();
        return;
    }
    const QPointF px = m_ctx.viewport->camera().project(*anchor);
    if(!QRectF(m_ctx.viewport->rect()).contains(px)) {
        m_box->hide();
        return;
    }
    m_box->showAt(px + QPointF(18, -20)); // clear of the handle, which stays grabbable
}

bool CommandController::eventFilter(QObject *o, QEvent *e) {
    if(o == m_ctx.viewport && m_cmd && e->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(e);
        const QString t = k->text();
        const bool valueKey = t.size() == 1 && (t[0].isDigit() || QStringLiteral(".,-+(").contains(t[0])) &&
                              !(k->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier));
        ValueField *f = m_cmd->canvasValue();
        if(valueKey && f) {
            // Type straight into the value, replacing it.
            QLineEdit *target = m_box->isVisible() ? static_cast<QLineEdit *>(m_box) : f;
            target->setFocus(Qt::OtherFocusReason);
            target->selectAll();
            QKeyEvent copy(QEvent::KeyPress, k->key(), k->modifiers(), t);
            QCoreApplication::sendEvent(target, &copy);
            return true;
        }
    }
    return QObject::eventFilter(o, e);
}

void CommandController::preview() {
    if(!m_cmd) return;
    if(!m_cmd->makesFeature()) {
        QString why;
        const bool ok = m_cmd->ready(why);
        m_ctx.panel->setOkEnabled(ok);
        m_ctx.panel->setMessage(why, why.isEmpty() ? cad::Severity::Ok : cad::Severity::Warning);
        m_cmd->showPreview();
        return;
    }
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
    if(!m_cmd) return false;
    // A value typed and Enter pressed straight away: the preview for it has not
    // been asked for yet, so bring OK up to date with the inputs first. (Once
    // a preview has failed, OK stays off until the inputs change.)
    if(m_previewPending && !m_ctx.panel->okButton()->isEnabled()) preview();
    if(!m_ctx.panel->okButton()->isEnabled()) return false; // not ready, or its preview failed
    if(!m_cmd->makesFeature()) {
        QString why;
        if(!m_cmd->ready(why)) return false;
        m_cmd->apply();
        finish();
        emit committed(cad::kNoFeature);
        return true;
    }
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
    if(m_cmd) m_cmd->end();
    if(m_cmd && m_ctx.viewport->tool() == m_cmd->tool()) m_ctx.viewport->setTool(nullptr);
    m_ctx.panel->end();
    m_box->bind(nullptr);
    m_box->hide();
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

} // namespace cadjitsu
