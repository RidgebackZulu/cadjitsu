#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "command/ThreadOptions.h"

#include "features/ThreadFeature.h"

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;

namespace cadjitsu {

// Thread: pick the round faces of holes (internal threads) or posts
// (external). The size is found from the diameter, or chosen (the recent
// sizes first); any hole or post is fitted to it and threaded, a hole with a
// countersink at its open ends. The clearance makes printed threads fit.
class ThreadCommand : public Command {
    Q_OBJECT

public:
    ThreadCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature) : Command(ctx, editing) {}

    QString title() const override { return tr("Thread"); }
    QString prompt() const override {
        return tr("Click the round faces of holes or posts to thread. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void accepted(const cad::Feature &f) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }
    void previewBusy(bool busy) override;
    ValueField *canvasValue() const override { return nullptr; }

    // Inputs (tests).
    int faceCount() const { return int(m_faces.size()); }
    QComboBox *sizeBox() const { return m_size; }
    const CountersinkRows &countersink() const { return m_csk; }
    bool busy() const { return m_busy; }
    QCheckBox *fullBox() const { return m_full; }
    ValueField *lengthField() const { return m_lengthField; }
    QLabel *detectedLabel() const { return m_detected; }

private:
    void updateRows();
    void updateMarks();
    void updateDetected();

    std::shared_ptr<const cad::ThreadFeature> m_original;
    std::vector<InputRef> m_faces;
    cad::ParamSlot m_clearance, m_length, m_cskDiameter;
    SelectionField *m_facesField = nullptr;
    QComboBox *m_size = nullptr;
    CountersinkRows m_csk;
    bool m_busy = false; // the preview is computing: the faces shown in yellow
    QLabel *m_detected = nullptr;
    ValueField *m_clearanceField = nullptr, *m_lengthField = nullptr;
    QCheckBox *m_full = nullptr, *m_left = nullptr;
};

} // namespace cadjitsu
