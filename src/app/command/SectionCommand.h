#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "command/Manipulator.h"

#include "doc/Section.h"

#include <optional>

class QCheckBox;

namespace cadjitsu {

// Fusion 360's Section Analysis: cut the model by an origin plane, a
// construction plane or a planar face, moved along its normal (type it or
// drag the arrow). The section is kept in the browser's Analysis folder, not
// the timeline; modelling goes on while it is shown.
class SectionCommand : public Command {
    Q_OBJECT

public:
    // `section` != 0 edits that section analysis.
    SectionCommand(const CommandContext &ctx, int section = 0);

    QString title() const override { return tr("Section Analysis"); }
    QString prompt() const override {
        return tr("Select a plane or planar face; drag the arrow or type the depth. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &) override { return nullptr; }
    bool makesFeature() const override { return false; }
    bool ready(QString &why) override;
    void showPreview() override;
    void apply() override;
    void end() override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    ViewportTool *tool() override { return &m_arrow; }
    // The on-canvas value box sits at the arrow's head.
    std::optional<QVector3D> canvasAnchor() const override {
        return m_arrow.visible() ? std::optional<QVector3D>(m_arrow.headPoint()) : std::nullopt;
    }

    // Where a section's depth arrow stands: on its plane (a face's middle, a
    // plane's centre, or the middle of the model), along the plane's normal.
    static bool arrowAxis(const cad::ModelState &state, const cad::PlaneRef &plane, QVector3D &origin,
                          QVector3D &direction);

    // Inputs (tests).
    bool hasPlane() const { return m_plane.has_value(); }
    ValueField *distanceField() const { return m_distanceField; }
    QCheckBox *flipBox() const { return m_flip; }
    DistanceManipulator &arrow() { return m_arrow; }

private:
    std::optional<cad::SectionAnalysis> current() const;
    void updateArrow();

    int m_section = 0;
    std::optional<InputRef> m_plane;
    SelectionField *m_planeField = nullptr;
    ValueField *m_distanceField = nullptr;
    QCheckBox *m_flip = nullptr;
    DistanceManipulator m_arrow;
};

} // namespace cadjitsu
