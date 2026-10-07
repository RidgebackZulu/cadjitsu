#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "viewport/ViewportTool.h"

#include "doc/ReferenceImage.h"

#include <optional>
#include <vector>

class QCheckBox;

namespace cadjitsu {

// Insert > Canvas, and editing one: a reference picture on a plane. Pick the
// plane (an origin plane, a construction plane or a planar face), then place
// the picture: its width, where its middle is (X / Y on the plane), its
// rotation, a left-right flip and how see-through it is. Kept in the
// browser's Canvases folder, not the timeline.
class CanvasCommand : public Command {
    Q_OBJECT

public:
    // Editing canvas `canvas`, or (0) inserting the picture `imageKey` of the
    // document, `pixels` in size.
    CanvasCommand(const CommandContext &ctx, int canvas, const std::string &imageKey = {}, QSize pixels = {});

    QString title() const override { return tr("Canvas"); }
    QString prompt() const override {
        return tr("Select the plane or face for the picture; set its width, place and opacity. Enter to finish.");
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

    // Inputs (tests).
    ValueField *widthField() const { return m_width; }
    ValueField *opacityField() const { return m_opacity; }
    int canvasId() const { return m_canvas; }

private:
    std::optional<cad::ReferenceImage> current() const;

    int m_canvas = 0;
    cad::ReferenceImage m_base; // what is not in the panel
    std::optional<InputRef> m_plane;
    SelectionField *m_planeField = nullptr;
    ValueField *m_width = nullptr, *m_x = nullptr, *m_y = nullptr, *m_rotation = nullptr, *m_opacity = nullptr;
    QCheckBox *m_flip = nullptr;
};

// Clicks on a canvas's plane, collected for calibration.
class CanvasPointsTool : public ViewportTool {
public:
    explicit CanvasPointsTool(class Viewport *vp) : m_vp(vp) {}
    bool mousePress(QMouseEvent *e) override;
    bool mouseMove(QMouseEvent *e) override;
    bool mouseRelease(QMouseEvent *e) override;
    void contribute(RenderScene &scene) override;
    void paintOverlay(QPainter &p) override;
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    gp_Ax3 frame;               // the canvas's plane
    bool hasFrame = false;
    int maxPoints = 2;
    bool closed = false;         // draw the points as a closed outline
    std::vector<cad::Vec2> points; // plane coordinates
    std::function<void()> onChanged;

private:
    std::optional<cad::Vec2> onPlane(QPointF px) const;
    Viewport *m_vp;
    std::optional<cad::Vec2> m_hover;
    bool m_pressed = false;
};

// Calibrate a canvas (click two marks a known distance apart, type the real
// distance) or correct its perspective (click the four corners of something
// rectangular, type its real width and height).
class CanvasCalibrateCommand : public Command {
    Q_OBJECT

public:
    enum class Mode { Scale, Perspective };
    CanvasCalibrateCommand(const CommandContext &ctx, int canvas, Mode mode);

    QString title() const override { return m_mode == Mode::Scale ? tr("Calibrate Canvas") : tr("Correct Perspective"); }
    QString prompt() const override;
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &) override { return nullptr; }
    bool makesFeature() const override { return false; }
    bool ready(QString &why) override;
    void showPreview() override;
    void apply() override;
    void end() override;
    ViewportTool *tool() override { return &m_tool; }
    std::optional<QVector3D> canvasAnchor() const override;

    // Tests: clicks at plane points, as the tool does.
    void addPoint(cad::Vec2 p);
    CanvasPointsTool &points() { return m_tool; }
    ValueField *distanceField() const { return m_distance; }
    ValueField *widthField() const { return m_width; }
    ValueField *heightField() const { return m_height; }

private:
    // The canvas as shown while picking (Perspective: the photo uncorrected).
    cad::ReferenceImage picking() const;

    int m_canvas = 0;
    Mode m_mode;
    CanvasPointsTool m_tool;
    SelectionField *m_pointsField = nullptr;
    ValueField *m_distance = nullptr, *m_width = nullptr, *m_height = nullptr;
};

} // namespace cadjitsu
