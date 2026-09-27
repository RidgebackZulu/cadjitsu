#pragma once

#include "command/Command.h"
#include "command/InputRef.h"

#include "features/ThreadFeature.h"

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;

namespace cadly {

// Thread: pick the round faces of holes (internal threads) or bosses
// (external). The size is found from the diameter, or chosen; small threads
// are left as a tap drill bore unless modelled on purpose. The clearance
// makes printed threads fit.
class ThreadCommand : public Command {
    Q_OBJECT

public:
    ThreadCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature) : Command(ctx, editing) {}

    QString title() const override { return tr("Thread"); }
    QString prompt() const override {
        return tr("Click the round faces of holes or bosses to thread. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }
    ValueField *canvasValue() const override { return nullptr; }

    // Inputs (tests).
    int faceCount() const { return int(m_faces.size()); }
    QComboBox *sizeBox() const { return m_size; }
    QComboBox *modeBox() const { return m_mode; }
    QCheckBox *fullBox() const { return m_full; }
    ValueField *lengthField() const { return m_lengthField; }
    QLabel *detectedLabel() const { return m_detected; }

private:
    void updateRows();
    void updateMarks();
    void updateDetected();

    std::shared_ptr<const cad::ThreadFeature> m_original;
    std::vector<InputRef> m_faces;
    cad::ParamSlot m_clearance, m_length;
    SelectionField *m_facesField = nullptr;
    QComboBox *m_size = nullptr, *m_mode = nullptr;
    QLabel *m_detected = nullptr;
    ValueField *m_clearanceField = nullptr, *m_lengthField = nullptr;
    QCheckBox *m_full = nullptr, *m_left = nullptr;
};

} // namespace cadly
