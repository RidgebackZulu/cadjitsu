#pragma once

#include "command/CommandPanel.h"
#include "model/RecomputeService.h"
#include "model/Selection.h"
#include "viewport/Picker.h"

#include "doc/Document.h"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <set>

namespace cadly {

class ModelView;
class Viewport;
class ViewportTool;

struct CommandContext {
    cad::Document *doc = nullptr;
    ModelView *view = nullptr;
    Viewport *viewport = nullptr;
    CommandPanel *panel = nullptr;
};

// A modelling command with its panel (Fusion's command dialog). The same
// class creates a feature and edits an existing one, so Edit Feature shows
// exactly the dialog the feature was made with.
class Command : public QObject {
    Q_OBJECT

public:
    Command(const CommandContext &ctx, cad::FeatureId editing);

    virtual QString title() const = 0;
    virtual IconId iconId() const = 0;
    // What to do, for the status bar.
    virtual QString prompt() const { return {}; }
    // Fills the panel (and reads the edited feature, or the current selection).
    virtual void setup() = 0;
    // The feature for the current inputs; nullptr (with `why`) if not ready.
    virtual std::shared_ptr<cad::Feature> build(QString &why) = 0;
    // The user clicked something in the canvas (nothing: empty space), or one
    // of the marks the command draws for its inputs.
    virtual void picked(const std::optional<SelectionItem> &, const PickHit &, Qt::KeyboardModifiers) {}
    virtual void markClicked(int) {}
    // Sketches to keep visible while the command runs (for picking profiles).
    virtual std::set<cad::FeatureId> sketchesToShow() const { return {}; }
    // Canvas manipulators (arrows, handles).
    virtual ViewportTool *tool() { return nullptr; }
    // The preview's model, once computed.
    virtual void previewed(const cad::StatePtr &) {}

    cad::FeatureId editing() const { return m_editing; }
    bool isEditing() const { return m_editing != cad::kNoFeature; }
    const CommandContext &context() const { return m_ctx; }

signals:
    void inputsChanged();

protected:
    // Evaluates value fields against the model parameters.
    ValueField::Evaluator evaluator() const;
    // The model the command starts from (before the edited feature), once the
    // background evaluation has produced it; null until then.
    cad::StatePtr baseState() const;

    CommandContext m_ctx;
    cad::FeatureId m_editing;
};

// Runs one command at a time: previews its candidate feature through the
// recompute service (as a what-if timeline), and commits it to the document
// (one undo step) or cancels it.
class CommandController : public QObject {
    Q_OBJECT

public:
    CommandController(const CommandContext &ctx, RecomputeService *recompute, QObject *parent = nullptr);
    ~CommandController() override;

    bool active() const { return m_cmd != nullptr; }
    // A preview request is queued (inputs changed and it has not been sent yet).
    bool previewPending() const { return m_previewPending; }
    Command *command() const { return m_cmd.get(); }
    void start(std::unique_ptr<Command> cmd);
    bool commit();
    void cancel();

    // Whether an evaluation belongs on screen right now.
    bool accepts(const EvaluationPtr &e) const;
    // Reports the candidate's status from a preview.
    void onEvaluation(const EvaluationPtr &e);
    // Index of the candidate in the preview timeline (-1 when none).
    int candidateIndex() const { return m_candidateIndex; }

signals:
    void activeChanged(bool active);
    void editingChanged(cad::FeatureId id);
    void committed(cad::FeatureId id);

private:
    void preview();
    void finish();

    CommandContext m_ctx;
    RecomputeService *m_recompute;
    std::unique_ptr<Command> m_cmd;
    uint64_t m_request = 0;
    int m_candidateIndex = -1;
    bool m_previewPending = false;
};

} // namespace cadly
