#pragma once

#include "command/Command.h"
#include "command/InputRef.h"

#include "features/AlignFeature.h"

#include <QVector3D>

#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QLabel;

namespace cadjitsu {

class AlignArrow;

// Align (Fusion's): pick the bodies to move, then geometry on them (FROM:
// a vertex, an edge or a face), then where it goes (TO: geometry elsewhere,
// or a plane). The panel's guide says what to click next, then what the pair
// will do; FROM shows orange, TO blue, with an arrow between them. Flip,
// Angle and Copy adjust it.
class AlignCommand : public Command {
    Q_OBJECT

public:
    AlignCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature);
    ~AlignCommand() override;

    QString title() const override { return tr("Align"); }
    QString prompt() const override;
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }
    ViewportTool *tool() override;
    ValueField *canvasValue() const override { return nullptr; }

    enum class Field { Objects, From, To };
    // Inputs (tests).
    Field activeField() const { return m_active; }
    int bodyCount() const { return int(m_bodies.size()); }
    bool hasFrom() const { return !m_from.empty(); }
    bool hasTo() const { return !m_to.empty(); }
    QLabel *guide() const { return m_guide; }
    QCheckBox *flipBox() const { return m_flip; }
    QCheckBox *copyBox() const { return m_copy; }
    ValueField *angleField() const { return m_angle; }

private:
    void activate(Field f);
    void updateMarks();
    void updateGuide();
    bool onMovedBody(const cad::BodyId &b) const;

    std::shared_ptr<const cad::AlignFeature> m_original;
    std::vector<cad::BodyId> m_bodies;
    cad::AlignRef m_from, m_to;
    Field m_active = Field::Objects;
    cad::ParamSlot m_angleSlot;
    std::unique_ptr<AlignArrow> m_arrow;

    QLabel *m_guide = nullptr;
    SelectionField *m_objectsField = nullptr, *m_fromField = nullptr, *m_toField = nullptr;
    QCheckBox *m_flip = nullptr, *m_copy = nullptr;
    ValueField *m_angle = nullptr;
};

} // namespace cadjitsu
