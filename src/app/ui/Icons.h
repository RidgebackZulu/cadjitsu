#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

#include <vector>

class QPainter;
class QRectF;

namespace cadly {

// Shaded, Fusion 360-style icons. Each one is an SVG in resources/icons
// (made by scripts/icons/gen_icons.py), rendered sharp at whatever size and
// device pixel ratio it is shown at.
enum class IconId {
    Home, Orbit, Pan, Zoom, Fit, Display, Grid, Camera,
    Sketch, FinishSketch, Line, Rectangle, CenterRectangle, Circle, Arc, Point, Dimension, Construction, LookAt,
    Coincident, Horizontal, Vertical, HorizontalVertical, Parallel, Perpendicular, Tangent, Equal, Midpoint,
    Concentric, Fix, Symmetric,
    Extrude, Fillet, Chamfer, Hole, Combine, Plane, Section, Measure, Overhang, Split, Move,
    Undo, Redo, Save, Open, New, ExportStl, ExportStep, Print3D,
    Eye, EyeOff, Body, SketchNode, PlaneNode, Folder, Warning, Error,
    TimelineFirst, TimelineBack, TimelineForward, TimelineLast, Origin, Flip,
    Settings, McpServer, Delete, Repeat,
};

// The colour the SVGs use for their recolourable parts (constraint glyphs);
// icon() swaps it for `accent`.
inline const QColor kIconAccent(47, 123, 224);

QIcon icon(IconId id, const QColor &accent = kIconAccent);

// The icon's file name in resources/icons, without ".svg" (e.g. "extrude").
QString iconName(IconId id);
std::vector<IconId> allIcons();

// The icon rendered to an image of `size` x `size` device pixels.
QImage iconImage(IconId id, int size, const QColor &accent = kIconAccent);

// A glyph chip (sketch constraint glyphs, snap hints): a small rounded white
// tile with a soft shadow and the icon inside.
void paintGlyphChip(QPainter &p, const QRectF &r, const QIcon &icon, const QColor &border, bool hot = false);

} // namespace cadly
