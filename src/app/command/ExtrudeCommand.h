#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "command/Manipulator.h"

#include "features/ExtrudeFeature.h"

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <array>
#include <map>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;

namespace cadjitsu {

// Fusion 360's Extrude: profiles and / or planar faces, one side / two sides
// / symmetric, Distance / To Object / All extents with taper angles, and the
// Join / Cut / Intersect / New Body operation (chosen automatically until the
// user picks one). A distance arrow on the canvas can be dragged.
class ExtrudeCommand : public Command {
    Q_OBJECT

public:
    ExtrudeCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature);
    // Profiles to start from instead of the canvas selection (a sketch just
    // finished with E).
    void setInitialProfiles(std::vector<cad::ProfileRef> profiles) { m_initial = std::move(profiles); }

    QString title() const override { return tr("Extrude"); }
    QString prompt() const override {
        return tr("Select profiles or planar faces; drag the arrow or type a distance. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void markClicked(int tag) override;
    std::set<cad::FeatureId> sketchesToShow() const override;
    ViewportTool *tool() override { return &m_arrow; }
    // The on-canvas value box sits at the arrow's head.
    std::optional<QVector3D> canvasAnchor() const override {
        return m_arrow.visible() ? std::optional<QVector3D>(m_arrow.headPoint()) : std::nullopt;
    }
    void previewed(const cad::StatePtr &state) override;

    // Inputs (tests).
    int profileCount() const { return int(m_refs[Profiles].size()); }
    bool hasObject() const { return !m_refs[Object].empty(); }
    cad::BodyOperation operation() const;
    QComboBox *directionBox() const { return m_direction; }
    QComboBox *extentBox() const { return m_extent; }
    QComboBox *operationBox() const { return m_operation; }
    ValueField *distanceField() const { return m_distanceField; }
    ValueField *taperField() const { return m_taperField; }
    QCheckBox *flipBox() const { return m_flip; }
    DistanceManipulator &arrow() { return m_arrow; }

private:
    enum Field { Profiles = 0, Object = 1, Object2 = 2 };

    void activate(Field f);
    void updateMarks();
    void updateRows();
    void updateArrow();
    bool chooseOperation(); // true if it changed the operation

public:
    // Whether a new extrude picks Join / Cut from where it goes (Fusion 360's
    // way). Off by default: extrudes make new bodies, combined afterwards.
    static bool autoOperation();
    static void setAutoOperation(bool on);

private:
    void changed();
    bool inputPoint(gp_Pnt &p, gp_Dir &n) const;

    std::shared_ptr<const cad::ExtrudeFeature> m_original;
    std::vector<cad::ProfileRef> m_initial;
    // Profiles and planar faces; the faces to extrude to (one each).
    std::array<std::vector<InputRef>, 3> m_refs;
    Field m_active = Profiles;
    cad::ParamSlot m_distance, m_distance2, m_taper, m_taper2;
    bool m_operationChosen = false;
    cad::ExtentType m_lastExtent = cad::ExtentType::Distance;
    // Inside / outside tests for the automatic operation, per base state.
    cad::StatePtr m_probed;
    std::map<std::array<double, 3>, bool> m_inside;

    std::array<SelectionField *, 3> m_fields{};
    QComboBox *m_direction = nullptr, *m_extent = nullptr, *m_extent2 = nullptr, *m_operation = nullptr;
    ValueField *m_distanceField = nullptr, *m_distance2Field = nullptr, *m_taperField = nullptr,
               *m_taper2Field = nullptr;
    QCheckBox *m_flip = nullptr;
    QLabel *m_sideTwo = nullptr;
    DistanceManipulator m_arrow;
};

} // namespace cadjitsu
