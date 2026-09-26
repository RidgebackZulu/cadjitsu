#pragma once

#include "command/Command.h"
#include "command/InputRef.h"

#include "features/CombineFeature.h"

#include <vector>

class QCheckBox;
class QComboBox;

namespace cadly {

// Fusion 360's Combine: join, cut or intersect a target body with tool bodies;
// Keep Tools leaves the tools in the model.
class CombineCommand : public Command {
    Q_OBJECT

public:
    CombineCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature) : Command(ctx, editing) {}

    QString title() const override { return tr("Combine"); }
    QString prompt() const override { return tr("Click the target body, then the tool bodies. Enter to finish."); }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void markClicked(int tag) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }

    // Inputs (tests).
    bool hasTarget() const { return !m_target.empty(); }
    int toolCount() const { return int(m_tools.size()); }
    QComboBox *operationBox() const { return m_operation; }
    QCheckBox *keepToolsBox() const { return m_keep; }

private:
    void activateTools(bool tools);
    void updateMarks();

    std::shared_ptr<const cad::CombineFeature> m_original;
    std::vector<InputRef> m_target; // one body
    std::vector<InputRef> m_tools;
    bool m_toolsActive = false;
    SelectionField *m_targetField = nullptr, *m_toolsField = nullptr;
    QComboBox *m_operation = nullptr;
    QCheckBox *m_keep = nullptr;
};

} // namespace cadly
