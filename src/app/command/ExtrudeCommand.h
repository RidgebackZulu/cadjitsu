#pragma once

#include "command/Command.h"
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

namespace cadly {

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
    void selectionChanged() override;
    std::set<cad::FeatureId> sketchesToShow() const override;
    ViewportTool *tool() override { return &m_arrow; }
    void previewed(const cad::StatePtr &state) override;

    // Inputs (tests).
    int profileCount() const { return int(m_profiles.size() + m_faces.size()); }
    cad::BodyOperation operation() const;
    QComboBox *directionBox() const { return m_direction; }
    QComboBox *extentBox() const { return m_extent; }
    QComboBox *operationBox() const { return m_operation; }
    ValueField *distanceField() const { return m_distanceField; }
    ValueField *taperField() const { return m_taperField; }
    QCheckBox *flipBox() const { return m_flip; }
    DistanceManipulator &arrow() { return m_arrow; }

private:
    enum class Field { Profiles, Object, Object2 };

    void activate(Field f);
    void showSelection();
    void updateRows();
    void updateArrow();
    bool chooseOperation(); // true if it changed the operation
    void changed();
    bool inputPoint(gp_Pnt &p, gp_Dir &n) const;

    std::shared_ptr<const cad::ExtrudeFeature> m_original;
    std::vector<cad::ProfileRef> m_initial;
    std::vector<cad::ProfileRef> m_profiles;
    std::vector<cad::TopoRef> m_faces;
    cad::TopoRef m_object, m_object2;
    Field m_active = Field::Profiles;
    cad::ParamSlot m_distance, m_distance2, m_taper, m_taper2;
    bool m_operationChosen = false;
    bool m_syncing = false;
    cad::ExtentType m_lastExtent = cad::ExtentType::Distance;
    // Inside / outside tests for the automatic operation, per base state.
    cad::StatePtr m_probed;
    std::map<std::array<double, 3>, bool> m_inside;

    SelectionField *m_profilesField = nullptr, *m_objectField = nullptr, *m_object2Field = nullptr;
    QComboBox *m_direction = nullptr, *m_extent = nullptr, *m_extent2 = nullptr, *m_operation = nullptr;
    ValueField *m_distanceField = nullptr, *m_distance2Field = nullptr, *m_taperField = nullptr,
               *m_taper2Field = nullptr;
    QCheckBox *m_flip = nullptr;
    QLabel *m_sideTwo = nullptr;
    DistanceManipulator m_arrow;
};

} // namespace cadly
