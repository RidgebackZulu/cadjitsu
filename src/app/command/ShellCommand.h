#pragma once

#include "command/Command.h"
#include "command/InputRef.h"

#include "features/ShellFeature.h"

#include <vector>

class QComboBox;

namespace cadjitsu {

// Shell: pick the faces to remove (the openings) and the wall thickness. A
// body picked without faces is hollowed with a sealed void.
class ShellCommand : public Command {
    Q_OBJECT

public:
    ShellCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature);

    QString title() const override { return tr("Shell"); }
    QString prompt() const override {
        return tr("Select the faces to remove (or a body to hollow); type the wall thickness. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void markClicked(int tag) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }

    // Inputs (tests).
    int faceCount() const { return int(m_faces.size()); }
    int bodyCount() const { return int(m_bodies.size()); }
    ValueField *thicknessField() const { return m_thicknessField; }
    QComboBox *directionBox() const { return m_direction; }

private:
    enum class Field { Faces, Bodies };
    void activate(Field f);
    void updateMarks();

    std::shared_ptr<const cad::ShellFeature> m_original;
    std::vector<InputRef> m_faces;
    std::vector<cad::BodyId> m_bodies;
    Field m_active = Field::Faces;
    cad::ParamSlot m_thickness;
    SelectionField *m_facesField = nullptr, *m_bodiesField = nullptr;
    ValueField *m_thicknessField = nullptr;
    QComboBox *m_direction = nullptr;
};

} // namespace cadjitsu
