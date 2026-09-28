#pragma once

#include "command/Command.h"
#include "command/InputRef.h"

#include "features/ChamferFeature.h"
#include "features/FilletFeature.h"

#include <vector>

class QCheckBox;
class QComboBox;

namespace cadjitsu {

// Fillet and Chamfer: edges, or faces (all of their edges), added and removed
// by clicking them. Edges the preview has rounded away stay drawn where they
// were and can still be clicked to drop them.
class EdgesCommand : public Command {
    Q_OBJECT

public:
    using Command::Command;

    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    void markClicked(int tag) override;
    void previewed(const cad::StatePtr &) override { updateMarks(); }

    int edgeCount() const { return int(m_refs.size()); }

protected:
    // Adds the Edges row, filled from the edited feature or the canvas selection.
    void setupEdges(const std::vector<cad::TopoRef> &edges, const std::vector<cad::TopoRef> &faces);
    void updateMarks();
    void splitRefs(std::vector<cad::TopoRef> &edges, std::vector<cad::TopoRef> &faces) const;
    bool needEdges(QString &why, const QString &message) const;

    std::vector<InputRef> m_refs;
    SelectionField *m_edgesField = nullptr;
};

// Fusion's Fillet: rounds edges with one radius; tangent edges are followed.
class FilletCommand : public EdgesCommand {
    Q_OBJECT

public:
    FilletCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature) : EdgesCommand(ctx, editing) {}

    QString title() const override { return tr("Fillet"); }
    QString prompt() const override { return tr("Select edges or faces to round, then type the radius. Enter to finish."); }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;

    ValueField *radiusField() const { return m_radiusField; }

private:
    std::shared_ptr<const cad::FilletFeature> m_original;
    cad::ParamSlot m_radius;
    ValueField *m_radiusField = nullptr;
};

// Fusion's Chamfer: equal distance, two distances, or a distance and an angle.
class ChamferCommand : public EdgesCommand {
    Q_OBJECT

public:
    ChamferCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature) : EdgesCommand(ctx, editing) {}

    QString title() const override { return tr("Chamfer"); }
    QString prompt() const override { return tr("Select edges or faces to bevel, then type the distance. Enter to finish."); }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;

    QComboBox *typeBox() const { return m_type; }
    ValueField *distanceField() const { return m_distanceField; }
    ValueField *distance2Field() const { return m_distance2Field; }
    ValueField *angleField() const { return m_angleField; }
    QCheckBox *flipBox() const { return m_flip; }

private:
    void updateRows();

    std::shared_ptr<const cad::ChamferFeature> m_original;
    cad::ParamSlot m_distance, m_distance2, m_angle;
    QComboBox *m_type = nullptr;
    ValueField *m_distanceField = nullptr, *m_distance2Field = nullptr, *m_angleField = nullptr;
    QCheckBox *m_flip = nullptr;
};

} // namespace cadjitsu
