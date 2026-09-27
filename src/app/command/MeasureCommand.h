#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "viewport/ViewportTool.h"

#include "measure/MeasureBetween.h"

#include <QVector3D>

#include <optional>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;

namespace cadly {

class Viewport;

// Draws the measure tool's result on the canvas: the dimension line between
// the closest points with its value, the X / Y / Z legs, and the line between
// centres. Clicks fall through to picking.
class MeasureOverlay : public ViewportTool {
public:
    explicit MeasureOverlay(Viewport *vp) : m_viewport(vp) {}

    struct Line {
        QVector3D a, b;
        QString label;
    };
    std::optional<Line> distance, centres;
    bool showLegs = false;
    std::vector<QVector3D> points; // picked points (ruler mode)

    void contribute(RenderScene &scene) override;
    void paintOverlay(QPainter &p) override;

private:
    Viewport *m_viewport;
};

// Inspect > Measure: pick one or two things (faces, edges, vertices, sketch
// points, whole bodies, or any points on surfaces) to see the distance between
// them, its X / Y / Z parts, the distance between centres (holes, circles)
// and the angle between flat faces or straight edges. One pick shows its own
// size (area, length, radius, volume). Adds nothing to the timeline.
class MeasureCommand : public Command {
    Q_OBJECT

public:
    enum class Mode { Entities, Bodies, Points };

    explicit MeasureCommand(const CommandContext &ctx);

    QString title() const override { return tr("Measure"); }
    QString prompt() const override {
        return tr("Click two faces, edges, points or bodies to measure between them; click empty space to start again.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &) override { return nullptr; }
    bool makesFeature() const override { return false; }
    bool ready(QString &) override { return true; }
    void showPreview() override {}
    void apply() override {}
    void end() override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    ViewportTool *tool() override { return &m_overlay; }
    ValueField *canvasValue() const override { return nullptr; }

    // Results (tests, MCP).
    int targetCount() const { return int(m_targets.size()); }
    const std::optional<cad::MeasureResult> &result() const { return m_result; }
    QComboBox *modeBox() const { return m_mode; }
    QComboBox *unitsBox() const { return m_units; }
    QLabel *distanceLabel() const { return m_distance; }
    QString resultText() const;
    void clear();

    // "12.35 mm" or "0.4862 in" (lengths in mm).
    static QString formatLength(double mm, bool inches);

private:
    struct Target {
        TopoDS_Shape shape;
        QString name;
        std::optional<InputRef> ref;       // for the highlight
        std::optional<QVector3D> point;    // a point picked on a surface / a sketch point
    };

    std::optional<Target> targetFor(const SelectionItem &item, const PickHit &hit) const;
    void applyFilter();
    void update();

    std::vector<Target> m_targets;
    std::optional<cad::MeasureResult> m_result;
    QComboBox *m_mode = nullptr, *m_units = nullptr;
    QLabel *m_first = nullptr, *m_second = nullptr, *m_distance = nullptr, *m_delta = nullptr, *m_centre = nullptr,
           *m_angle = nullptr, *m_props = nullptr;
    QPushButton *m_copy = nullptr;
    MeasureOverlay m_overlay;
};

} // namespace cadly
