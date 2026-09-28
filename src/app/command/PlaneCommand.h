#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "command/PlaneGizmo.h"

#include "features/ConstructionPlaneFeature.h"

#include <optional>

class QComboBox;

namespace cadjitsu {

// Fusion 360's Offset Plane, which can also turn the plane: a construction
// plane off an origin plane, a construction plane or a planar face, moved
// along its normal (type it or drag the arrow) and tilted about its own X
// axis, its Y axis or both (type the angles, turn the dials in the panel or
// drag the rings on the canvas), or turned about a straight edge.
class PlaneCommand : public Command {
    Q_OBJECT

public:
    PlaneCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature);

    QString title() const override { return tr("Offset Plane"); }
    QString prompt() const override {
        return tr("Select a plane or planar face; type the offset or drag the arrow, and tilt it with the red and "
                  "green rings (Shift: 15° steps) or the angle boxes. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    ViewportTool *tool() override { return &m_gizmo; }
    // The on-canvas value box is the distance, at the arrow's head, or the
    // tilt of the ring last hovered or dragged, at its knob.
    ValueField *canvasValue() const override;
    std::optional<QVector3D> canvasAnchor() const override;
    QColor canvasAccent() const override;
    void previewed(const cad::StatePtr &) override;

    // Inputs (tests).
    bool hasBase() const { return m_base.has_value(); }
    bool hasEdge() const { return m_edge.has_value(); }
    ValueField *offsetField() const { return m_offsetField; }
    ValueField *angleField() const { return m_angleField; } // Tilt X (the angle about an edge)
    ValueField *tiltYField() const { return m_angleYField; }
    QComboBox *axisBox() const { return m_axis; }
    bool edgeMode() const;
    DistanceManipulator &arrow() { return m_gizmo.arrow(); }
    PlaneGizmo &gizmo() { return m_gizmo; }

private:
    enum class Field { Base, Edge };

    void activate(Field f);
    void updateRows();
    void updateMarks();
    void updateArrow();
    int ringOf(PlaneGizmo::Part p) const;

    std::shared_ptr<const cad::ConstructionPlaneFeature> m_original;
    std::optional<InputRef> m_base, m_edge;
    Field m_active = Field::Base;
    cad::ParamSlot m_offset, m_angle, m_angleY;
    SelectionField *m_baseField = nullptr, *m_edgeField = nullptr;
    ValueField *m_offsetField = nullptr, *m_angleField = nullptr, *m_angleYField = nullptr;
    QComboBox *m_axis = nullptr;
    PlaneGizmo m_gizmo;
};

} // namespace cadjitsu
