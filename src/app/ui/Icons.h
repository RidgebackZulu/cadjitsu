#pragma once

#include <QColor>
#include <QIcon>

namespace cadly {

// Procedurally drawn icons (no image assets), styled after Fusion 360's
// flat blue-grey icon set.
enum class IconId {
    Home, Orbit, Pan, Zoom, Fit, Display, Grid, Camera,
    Sketch, FinishSketch, Line, Rectangle, CenterRectangle, Circle, Arc, Point, Dimension, Construction, LookAt,
    Coincident, Horizontal, Vertical, HorizontalVertical, Parallel, Perpendicular, Tangent, Equal, Midpoint,
    Concentric, Fix, Symmetric,
    Extrude, Fillet, Chamfer, Hole, Combine, Plane, Section, Measure,
    Undo, Redo, Save, Open, New, ExportStl, ExportStep,
    Eye, EyeOff, Body, SketchNode, PlaneNode, Folder, Warning, Error,
};

QIcon icon(IconId id, const QColor &accent = QColor(38, 110, 196));

} // namespace cadly
