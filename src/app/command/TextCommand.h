#pragma once

#include "command/Command.h"
#include "command/InputRef.h"
#include "command/PlaneGizmo.h"

#include "features/TextFeature.h"

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QPlainTextEdit;

namespace cadjitsu {

// The Emboss Text command's canvas controls: the letters' outlines on the
// face (drawn at once, before the solid preview follows), a round move knob
// at the text's middle that drags it over the face, the rotation ring about
// the face normal and the depth arrow.
class TextGizmo : public ViewportTool {
public:
    explicit TextGizmo(Viewport *viewport);

    PlaneGizmo &plane() { return m_plane; }
    const PlaneGizmo &plane() const { return m_plane; }

    // The knob, and the frame it moves the text in.
    void setFrame(const std::optional<cad::FaceTextFrame> &frame) { m_frame = frame; }
    void setKnob(double x, double y) {
        m_x = x;
        m_y = y;
    }
    QVector3D knobPoint() const;
    QPointF knobOnScreen() const;
    bool knobVisible() const { return m_frame.has_value() && m_visible; }
    void setVisible(bool on) { m_visible = on; }
    bool movingKnob() const { return m_moving; }
    void setOutlines(std::vector<std::vector<QVector3D>> outlines) { m_outlines = std::move(outlines); }
    const std::vector<std::vector<QVector3D>> &outlines() const { return m_outlines; }

    std::function<void(double x, double y)> onMove; // the text's middle, while the knob is dragged
    std::function<void()> onRelease;

    bool mousePress(QMouseEvent *e) override;
    bool mouseMove(QMouseEvent *e) override;
    bool mouseRelease(QMouseEvent *e) override;
    void contribute(RenderScene &scene) override;
    void paintOverlay(QPainter &p) override;
    Qt::CursorShape cursor() const override;

private:
    bool nearKnob(QPointF px) const;
    // Where the mouse is over the face, in its frame.
    bool frameAt(QPointF px, double &x, double &y) const;

    Viewport *m_viewport;
    PlaneGizmo m_plane;
    std::optional<cad::FaceTextFrame> m_frame;
    double m_x = 0, m_y = 0, m_grabX = 0, m_grabY = 0;
    bool m_visible = false, m_moving = false, m_hot = false;
    std::vector<std::vector<QVector3D>> m_outlines;
};

// Text engraved into or embossed onto any face: pick the face (where it is
// clicked is where the text goes) and type. Font, size, spacing, several
// lines, rotation, depth, engrave / emboss and Reverse (mirrored letters)
// are set in the panel; the knob, ring and arrow on the canvas move, turn
// and deepen it. On a curved face the letters follow the surface.
class TextCommand : public Command {
    Q_OBJECT

public:
    TextCommand(const CommandContext &ctx, cad::FeatureId editing = cad::kNoFeature);

    QString title() const override { return tr("Emboss Text"); }
    QString prompt() const override {
        return tr("Click the face for the text and type it. Drag the knob to move it, the ring to turn it and the "
                  "arrow for its depth. Enter to finish.");
    }
    IconId iconId() const override;
    void setup() override;
    std::shared_ptr<cad::Feature> build(QString &why) override;
    void picked(const std::optional<SelectionItem> &item, const PickHit &hit, Qt::KeyboardModifiers mods) override;
    ViewportTool *tool() override { return &m_gizmo; }
    ValueField *canvasValue() const override;
    std::optional<QVector3D> canvasAnchor() const override;
    QColor canvasAccent() const override;
    void previewed(const cad::StatePtr &) override;

    // Inputs (tests).
    bool hasFace() const { return m_face.has_value(); }
    QPlainTextEdit *textBox() const { return m_text; }
    QComboBox *fontBox() const { return m_font; }
    QComboBox *directionBox() const { return m_direction; }
    QCheckBox *reverseBox() const { return m_reverse; }
    ValueField *sizeField() const { return m_sizeField; }
    ValueField *xField() const { return m_xField; }
    ValueField *yField() const { return m_yField; }
    ValueField *rotationField() const { return m_rotationField; }
    ValueField *depthField() const { return m_depthField; }
    TextGizmo &gizmo() { return m_gizmo; }
    // What the canvas shows before the solid preview (the letters' outlines).
    const cad::TextOnFace &laidOut() const { return m_laid; }

private:
    void updateMarks();
    // Lays the text out on the face again and moves the handles to it.
    void updateCanvas();
    std::optional<TopoDS_Face> faceShape() const;
    bool placement(cad::TextPlacementInput &in) const;

    std::shared_ptr<const cad::TextFeature> m_original;
    std::optional<InputRef> m_face;
    cad::ParamSlot m_size, m_letterSpacing, m_lineSpacing, m_x, m_y, m_rotation, m_depth;
    SelectionField *m_faceField = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QComboBox *m_font = nullptr, *m_direction = nullptr;
    QCheckBox *m_bold = nullptr, *m_italic = nullptr, *m_reverse = nullptr;
    ValueField *m_sizeField = nullptr, *m_letterSpacingField = nullptr, *m_lineSpacingField = nullptr,
               *m_xField = nullptr, *m_yField = nullptr, *m_rotationField = nullptr, *m_depthField = nullptr;
    TextGizmo m_gizmo;
    cad::TextOnFace m_laid;
    bool m_marked = false; // whether the marks were made with the text laid out
};

} // namespace cadjitsu
