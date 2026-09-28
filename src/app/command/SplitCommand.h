#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "viewport/ViewportTool.h"

#include "features/SplitFeature.h"

#include <QVector3D>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;

namespace cadjitsu {

class Viewport;

// Draws the sketch curves picked for a split (they have no model marks).
class CurveMarks : public ViewportTool {
public:
    std::vector<std::vector<QVector3D>> polylines;
    void contribute(RenderScene &scene) override;
};

// Split Body: cut bodies in two with a plane (origin plane, construction
// plane or planar face) or sketch curves; each piece becomes a body. Keep
// both sides or one; optionally drill alignment pin holes into both halves
// of a plane cut, for parts printed in pieces.
class SplitCommand : public Command {
    Q_OBJECT

public:
    SplitCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature) : Command(ctx, editing) {}

    QString title() const override { return tr("Split Body"); }
    QString prompt() const override {
        return tr("Pick the plane or sketch curves to split with; optionally pick the bodies (else every body it crosses). "
                  "Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void markClicked(int tag) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }
    std::set<cad::FeatureId> sketchesToShow() const override;
    ViewportTool *tool() override { return &m_curveMarks; }

    // Inputs (tests).
    bool hasPlane() const { return m_plane.has_value(); }
    int bodyCount() const { return int(m_bodies.size()); }
    int curveCount() const { return int(m_curves.size()); }
    QComboBox *toolBox() const { return m_toolBox; }
    QComboBox *keepBox() const { return m_keep; }
    QCheckBox *pinsBox() const { return m_pins; }

private:
    enum class Field { Tool, Bodies };
    bool sketchMode() const;
    void activate(Field f);
    void updateRows();
    void updateMarks();

    std::shared_ptr<const cad::SplitFeature> m_original;
    std::optional<InputRef> m_plane;
    cad::FeatureId m_sketch = cad::kNoFeature;
    std::vector<int> m_curves;
    std::vector<InputRef> m_bodies;
    Field m_active = Field::Tool;
    cad::ParamSlot m_pinDiameter, m_pinDepth;
    SelectionField *m_bodiesField = nullptr, *m_planeField = nullptr, *m_curvesField = nullptr;
    QComboBox *m_toolBox = nullptr, *m_keep = nullptr;
    QCheckBox *m_pins = nullptr;
    ValueField *m_pinDiameterField = nullptr, *m_pinDepthField = nullptr;
    CurveMarks m_curveMarks;
};

} // namespace cadjitsu
