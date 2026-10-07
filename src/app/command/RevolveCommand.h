#pragma once

#include "command/Command.h"
#include "command/InputRef.h"

#include "features/RevolveFeature.h"

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;

namespace cadjitsu {

// Revolve: pick sketch profiles (or planar faces), then the axis (a sketch
// line, a straight edge or cylinder of a body, or the X / Y / Z axis), and
// the angle (360 by default: a full turn).
class RevolveCommand : public Command {
    Q_OBJECT

public:
    RevolveCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature);

    QString title() const override { return tr("Revolve"); }
    QString prompt() const override {
        return tr("Select the profiles to revolve, then the axis (a sketch line, an edge, or X / Y / Z in the "
                  "panel); set the angle. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void markClicked(int tag) override;
    std::set<cad::FeatureId> sketchesToShow() const override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }
    ValueField *canvasValue() const override { return m_angleField; }

    // Inputs (tests).
    int profileCount() const { return int(m_profiles.size()); }
    bool hasAxis() const;
    ValueField *angleField() const { return m_angleField; }
    QComboBox *operationBox() const { return m_operation; }

private:
    enum class Field { Profiles, Axis };
    void activate(Field f);
    void updateMarks();
    void updateRows();
    cad::BodyOperation operation() const;

    std::shared_ptr<const cad::RevolveFeature> m_original;
    std::vector<InputRef> m_profiles;
    std::optional<InputRef> m_axisRef;           // an edge or face
    cad::FeatureId m_axisSketch = cad::kNoFeature; // or a sketch line
    int m_axisLine = 0;
    Field m_active = Field::Profiles;
    cad::ParamSlot m_angle, m_angle2;
    SelectionField *m_profilesField = nullptr, *m_axisField = nullptr;
    QComboBox *m_axisKind = nullptr, *m_extent = nullptr, *m_operation = nullptr;
    ValueField *m_angleField = nullptr, *m_angle2Field = nullptr;
    QCheckBox *m_flip = nullptr;
};

} // namespace cadjitsu
