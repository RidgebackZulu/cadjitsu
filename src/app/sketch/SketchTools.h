#pragma once

#include "sketch/SketchEditor.h"
#include "viewport/ViewportTool.h"

#include <QCoreApplication>
#include <QColor>
#include <QPointF>
#include <QString>

#include <memory>
#include <vector>

class QKeyEvent;

namespace cadly {

class HeadsUpInput;
class SketchMode;

enum class SketchToolKind {
    Select,
    Line,
    Rectangle,
    CenterRectangle,
    Circle,
    Arc,
    Point,
    Dimension,
    Coincident,
    HorizontalVertical,
    Parallel,
    Perpendicular,
    Tangent,
    Equal,
    Midpoint,
    Concentric,
    Fix,
    Symmetric,
    Move,
};

QString sketchToolName(SketchToolKind kind);

// Base of the tools used while a sketch is open. Every tool draws the sketch
// (through the editor) and adds its own preview on top.
class SketchTool : public ViewportTool {
    Q_DECLARE_TR_FUNCTIONS(SketchTool)

public:
    explicit SketchTool(SketchMode &mode) : m_mode(mode) {}
    SketchEditor &editor() const;
    HeadsUpInput &hud() const;

    virtual SketchToolKind kind() const = 0;
    // Status bar hint.
    virtual QString prompt() const { return {}; }
    virtual void activate() {}
    // Abandons an operation in progress; false if there was none.
    virtual bool cancel() { return false; }
    // Enter in a heads-up box, and a typed value changing.
    virtual void hudCommit() {}
    virtual void hudChanged() {}

    void contribute(RenderScene &scene) override;
    void paintOverlay(QPainter &p) override;
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

protected:
    virtual void contributeTool(RenderScene &) {}
    virtual void paintToolOverlay(QPainter &) {}
    void setHover(QPointF px, unsigned filter);
    // Esc, Delete and digits (which start typing into the heads-up boxes).
    bool commonKey(QKeyEvent *e);
    static bool isValueKey(const QKeyEvent *e);
    // Draws picked entities emphasised (constraint and dimension tools).
    void highlight(RenderScene &scene, const std::vector<SketchHit> &hits, const QColor &color) const;

    SketchMode &m_mode;
};

std::unique_ptr<SketchTool> createSketchTool(SketchMode &mode, SketchToolKind kind);

} // namespace cadly
