#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "command/Manipulator.h"

#include "features/ConstructionPlaneFeature.h"

#include <optional>

class QComboBox;

namespace cadly {

// Fusion 360's Offset Plane, which can also turn the plane: a construction
// plane off an origin plane, a construction plane or a planar face, moved
// along its normal (type it or drag the arrow) and rotated by any angle about
// its own X or Y axis or about a straight edge.
class PlaneCommand : public Command {
    Q_OBJECT

public:
    PlaneCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature);

    QString title() const override { return tr("Offset Plane"); }
    QString prompt() const override {
        return tr("Select a plane or planar face; type the offset (or drag the arrow) and an angle. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    ViewportTool *tool() override { return &m_arrow; }
    void previewed(const cad::StatePtr &) override;

    // Inputs (tests).
    bool hasBase() const { return m_base.has_value(); }
    bool hasEdge() const { return m_edge.has_value(); }
    ValueField *offsetField() const { return m_offsetField; }
    ValueField *angleField() const { return m_angleField; }
    QComboBox *axisBox() const { return m_axis; }
    DistanceManipulator &arrow() { return m_arrow; }

private:
    enum class Field { Base, Edge };

    void activate(Field f);
    void updateRows();
    void updateMarks();
    void updateArrow();

    std::shared_ptr<const cad::ConstructionPlaneFeature> m_original;
    std::optional<InputRef> m_base, m_edge;
    Field m_active = Field::Base;
    cad::ParamSlot m_offset, m_angle;
    SelectionField *m_baseField = nullptr, *m_edgeField = nullptr;
    ValueField *m_offsetField = nullptr, *m_angleField = nullptr;
    QComboBox *m_axis = nullptr;
    DistanceManipulator m_arrow;
};

} // namespace cadly
