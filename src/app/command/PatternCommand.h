#pragma once

#include "command/Command.h"
#include "command/InputRef.h"

#include "features/PatternFeature.h"

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;

namespace cadly {

// Mirror, Rectangular Pattern and Circular Pattern (one command, three
// kinds): repeat bodies, or features (holes and extrudes: click one of their
// faces), across a plane, along one or two directions, or around an axis.
class PatternCommand : public Command {
    Q_OBJECT

public:
    PatternCommand(const CommandContext &ctx, cad::PatternKind kind, cad::FeatureId editing = cad::kNoFeature)
        : Command(ctx, editing), m_kind(kind) {}

    QString title() const override;
    QString prompt() const override;
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }

    // Inputs (tests).
    QComboBox *objectsBox() const { return m_objects; }
    int bodyCount() const { return int(m_bodies.size()); }
    int featureCount() const { return int(m_features.size()); }
    const std::vector<cad::FeatureId> &featureIds() const { return m_features; }
    bool hasPlane() const { return m_plane.has_value(); }
    QComboBox *dir1Box() const { return m_dir1; }
    QComboBox *axisBox() const { return m_axisBox; }
    ValueField *count1Field() const { return m_count1; }
    ValueField *spacing1Field() const { return m_spacing1; }
    ValueField *countField() const { return m_count; }
    ValueField *angleField() const { return m_angle; }
    QCheckBox *secondBox() const { return m_second; }
    QCheckBox *joinBox() const { return m_join; }

private:
    enum class Field { Objects, Plane, Dir1, Dir2, Axis };
    bool featuresMode() const;
    void activate(Field f);
    void updateRows();
    void updateMarks();
    // The feature a picked face was made by, if it can be repeated.
    std::optional<cad::FeatureId> featureOfFace(const SelectionItem &item) const;

    cad::PatternKind m_kind;
    std::shared_ptr<const cad::PatternFeature> m_original;
    std::vector<InputRef> m_bodies;
    std::vector<cad::FeatureId> m_features;
    std::optional<InputRef> m_plane, m_dir1Ref, m_dir2Ref, m_axisRef;
    Field m_active = Field::Objects;
    cad::ParamSlot m_count1Slot, m_spacing1Slot, m_count2Slot, m_spacing2Slot, m_countSlot, m_angleSlot;

    QComboBox *m_objects = nullptr, *m_dir1 = nullptr, *m_dir2 = nullptr, *m_axisBox = nullptr;
    SelectionField *m_objectsField = nullptr, *m_planeField = nullptr, *m_dir1Field = nullptr, *m_dir2Field = nullptr,
                   *m_axisField = nullptr;
    ValueField *m_count1 = nullptr, *m_spacing1 = nullptr, *m_count2 = nullptr, *m_spacing2 = nullptr,
               *m_count = nullptr, *m_angle = nullptr;
    QCheckBox *m_second = nullptr, *m_symmetric = nullptr, *m_join = nullptr;
};

} // namespace cadly
