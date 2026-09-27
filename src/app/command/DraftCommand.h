#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "command/PlaneGizmo.h"

#include "features/DraftFeature.h"

#include <optional>
#include <vector>

class QCheckBox;

namespace cadly {

// Draft: tilt flat faces about a hinge edge. Pick the faces, then the hinge
// (the straight edges of the first face are offered); set the angle by typing,
// turning the dial or dragging the ring on the hinge. A positive angle leans
// the faces in over the body (a taper that prints without support).
class DraftCommand : public Command {
    Q_OBJECT

public:
    DraftCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature);

    QString title() const override { return tr("Draft"); }
    QString prompt() const override {
        return tr("Select the faces to tilt, then the hinge edge they turn about; drag the ring or type the angle. "
                  "Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void markClicked(int tag) override;
    void previewed(const cad::StatePtr &) override {
        updateMarks();
        updateRing();
    }
    ViewportTool *tool() override { return &m_gizmo; }
    ValueField *canvasValue() const override { return m_angleField; }
    std::optional<QVector3D> canvasAnchor() const override;

    // Inputs (tests).
    int faceCount() const { return int(m_faces.size()); }
    bool hasHinge() const { return m_hinge.has_value(); }
    ValueField *angleField() const { return m_angleField; }
    QCheckBox *flipBox() const { return m_flip; }
    PlaneGizmo &gizmo() { return m_gizmo; }

private:
    enum class Field { Faces, Hinge };
    void activate(Field f);
    void updateMarks();
    void updateRing();

    std::shared_ptr<const cad::DraftFeature> m_original;
    std::vector<InputRef> m_faces;
    std::optional<InputRef> m_hinge;
    std::vector<cad::TopoRef> m_candidates; // straight edges of the first face (clickable marks)
    Field m_active = Field::Faces;
    cad::ParamSlot m_angle;
    SelectionField *m_facesField = nullptr, *m_hingeField = nullptr;
    ValueField *m_angleField = nullptr;
    QCheckBox *m_flip = nullptr;
    PlaneGizmo m_gizmo;
};

} // namespace cadly
