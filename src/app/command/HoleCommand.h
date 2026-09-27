#pragma once

#include "command/Command.h"
#include "command/InputRef.h"

#include "features/HoleFeature.h"

#include <optional>
#include <vector>

class QComboBox;
class QLabel;

namespace cadly {

// Fusion 360's Hole: click a planar face to place a hole (each click adds
// one; click a centre mark to remove it; X / Y set the last one exactly), or
// pick sketch points. Simple, counterbore or countersink holes, to a depth
// or through everything, with a flat or angled drill point.
class HoleCommand : public Command {
    Q_OBJECT

public:
    HoleCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature) : Command(ctx, editing) {}

    QString title() const override { return tr("Hole"); }
    QString prompt() const override {
        return tr("Click a planar face to place holes (or pick sketch points); set the size. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void markClicked(int tag) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }
    std::set<cad::FeatureId> sketchesToShow() const override;

    // Inputs (tests).
    int holeCount() const { return atSketchPoints() ? int(m_sketchPoints.size()) : int(m_points.size()); }
    const std::vector<cad::Vec2> &points() const { return m_points; }
    bool atSketchPoints() const;
    QComboBox *placementBox() const { return m_placement; }
    QComboBox *typeBox() const { return m_type; }
    QComboBox *extentBox() const { return m_extent; }
    QComboBox *tipBox() const { return m_tip; }
    ValueField *diameterField() const { return m_diameterField; }
    ValueField *canvasValue() const override;
    QComboBox *threadSizeBox() const { return m_threadSize; }
    QComboBox *threadModeBox() const { return m_threadMode; }
    QLabel *threadInfo() const { return m_threadInfo; }
    ValueField *depthField() const { return m_depthField; }
    ValueField *xField() const { return m_x; }
    ValueField *yField() const { return m_y; }

private:
    void updateRows();
    void updateMarks();
    void showPosition();
    void usePlacementFilter();
    // The frame hole positions on the face are measured in.
    std::optional<gp_Ax3> faceFrame(const cad::ModelState &st) const;

    std::shared_ptr<const cad::HoleFeature> m_original;
    std::optional<InputRef> m_face;       // placement face
    std::vector<cad::Vec2> m_points;      // on it
    std::vector<InputRef> m_sketchPoints; // or sketch points (of one sketch)
    cad::ParamSlot m_diameter, m_depth, m_cboreDiameter, m_cboreDepth, m_csinkDiameter, m_csinkAngle, m_tipAngle,
        m_threadClearance;

    QComboBox *m_placement = nullptr, *m_type = nullptr, *m_extent = nullptr, *m_tip = nullptr, *m_threadSize = nullptr,
              *m_threadMode = nullptr;
    ValueField *m_threadClearanceField = nullptr;
    QLabel *m_threadInfo = nullptr;
    SelectionField *m_position = nullptr;
    ValueField *m_x = nullptr, *m_y = nullptr;
    ValueField *m_diameterField = nullptr, *m_depthField = nullptr, *m_cboreDiameterField = nullptr,
               *m_cboreDepthField = nullptr, *m_csinkDiameterField = nullptr, *m_csinkAngleField = nullptr,
               *m_tipAngleField = nullptr;
};

} // namespace cadly
