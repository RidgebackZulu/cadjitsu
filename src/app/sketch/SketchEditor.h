#pragma once

#include "viewport/RenderScene.h"

#include "expr/ParamTable.h"
#include "features/SketchFeature.h"
#include "sketch/ProfileBuilder.h"
#include "sketch/SketchSolver.h"

#include <gp_Ax3.hxx>

#include <QMatrix4x4>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector3D>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class QPainter;

namespace cadly {

class Viewport;

struct SketchHit {
    enum class Kind { None, Point, Curve, Origin, Axis, Dimension, Constraint, Profile };
    Kind kind = Kind::None;
    // Entity or constraint id; kSketchOrigin / kSketchXAxis / kSketchYAxis; or a profile index.
    int id = 0;
    bool operator==(const SketchHit &) const = default;
    bool valid() const { return kind != Kind::None; }
    // Points, curves, the origin and the axes: things constraints can refer to.
    bool isGeometry() const {
        return kind == Kind::Point || kind == Kind::Curve || kind == Kind::Origin || kind == Kind::Axis;
    }
};

// What hitTest() may return.
enum SketchHitFilter : unsigned {
    HitPoints = 1u << 0,
    HitCurves = 1u << 1,
    HitOrigin = 1u << 2,
    HitAxes = 1u << 3,
    HitDimensions = 1u << 4,
    HitConstraints = 1u << 5,
    HitProfiles = 1u << 6,
    HitDefault = HitPoints | HitCurves | HitOrigin | HitDimensions | HitConstraints | HitProfiles,
    HitGeometry = HitPoints | HitCurves | HitOrigin | HitAxes,
};

// Where a point being drawn lands and what it attaches to.
struct SketchSnap {
    enum class Kind { None, Point, Origin, Midpoint, Quadrant, OnCurve, Grid };
    Kind kind = Kind::None;
    cad::Vec2 pos;
    int entity = 0; // point id (Point) or curve id (Midpoint / Quadrant / OnCurve; may be an axis)
    bool attaches() const { return kind != Kind::None && kind != Kind::Grid; }
};

// A constraint a tool would like to add. It is kept only if the sketch still
// solves without becoming over-constrained.
struct PendingConstraint {
    PendingConstraint(cad::SkCon t = cad::SkCon::Coincident, int a = 0, int b = 0, int c = 0)
        : type(t), e1(a), e2(b), e3(c) {}
    cad::SkCon type = cad::SkCon::Coincident;
    int e1 = 0, e2 = 0, e3 = 0;
    std::string expr;       // dimensions: the value expression, e.g. "20 mm" or "d1 * 2"
    cad::Vec2 label;        // dimensions: label offset from the anchor
    bool supplementary = false;
};

struct SketchDisplayOptions {
    bool grid = true;
    bool snapToGrid = false;
    bool profiles = true;
    bool points = true;
    bool dimensions = true;
    bool constraints = true;
};

// The working copy of a sketch while it is being edited: geometry, local
// undo, solving, snapping, hit testing and drawing (3D curves plus a 2D
// overlay of dimensions and constraint glyphs). The sketch mode commits the
// copy back into the document when the sketch is finished.
class SketchEditor : public QObject {
    Q_OBJECT

public:
    // Builds the model's parameter table with a candidate sketch substituted in.
    using ParamProvider = std::function<std::shared_ptr<cad::ParamTable>(const cad::SketchFeature &)>;
    // Hands out a fresh model parameter name ("d7") for a new dimension.
    using ParamAllocator = std::function<std::string()>;

    SketchEditor(Viewport *viewport, const cad::SketchFeature &feature, const gp_Ax3 &frame, ParamProvider params,
                 ParamAllocator allocate, QObject *parent = nullptr);

    cad::Sketch &sketch() { return m_feature->sketch; }
    const cad::Sketch &sketch() const { return m_feature->sketch; }
    std::shared_ptr<cad::SketchFeature> feature() const { return m_feature; }
    cad::FeatureId featureId() const { return m_feature->id; }
    const gp_Ax3 &frame() const { return m_frame; }
    Viewport *viewport() const { return m_viewport; }
    SketchDisplayOptions &options() { return m_options; }
    const SketchDisplayOptions &options() const { return m_options; }
    void setOptions(const SketchDisplayOptions &o);

    // --- coordinates ----------------------------------------------------------------
    std::optional<cad::Vec2> toSketch(QPointF px) const;
    QPointF toScreen(cad::Vec2 p) const;
    QVector3D toWorld(cad::Vec2 p) const;
    double sketchUnitsPerPixel(cad::Vec2 near) const;
    QMatrix4x4 frameMatrix() const;
    // Position of a point entity or the origin.
    cad::Vec2 posOf(int pointId) const;

    // --- editing --------------------------------------------------------------------
    // Applies an edit as one undo step and re-solves (keeping the `hold`
    // points where they are if it can). Returns false (restoring the previous
    // state) if the result cannot be solved, or is newly over-constrained when
    // `rejectIfOverConstrained`.
    bool edit(const QString &label, const std::function<void(cad::Sketch &)> &fn, bool rejectIfOverConstrained = true,
              const std::vector<int> &hold = {});
    // Replaces the sketch with `work` plus whichever `extra` constraints keep it
    // solvable, as one undo step. Returns the ids of the constraints added (0 =
    // dropped) through `added`.
    bool commit(const QString &label, cad::Sketch work, const std::vector<PendingConstraint> &extra,
                std::vector<int> *added = nullptr);
    void setSketch(const cad::Sketch &s); // no undo step
    // Move / Copy: entities (points, lines, arcs, circles) turned by `angle`
    // (radians) about `pivot`, then shifted by `delta`, as one undo step. A
    // copy keeps the originals and copies the constraints and dimensions
    // among the copied geometry. False (nothing changed) if the sketch cannot
    // be solved that way.
    bool moveEntities(const std::set<int> &ids, cad::Vec2 pivot, cad::Vec2 delta, double angle, bool copy);
    // Where those entities would be drawn after such a move (preview segments).
    std::vector<std::pair<cad::Vec2, cad::Vec2>> movedOutline(const std::set<int> &ids, cad::Vec2 pivot, cad::Vec2 delta,
                                                              double angle) const;
    // Adds a driving dimension with the current measured value; falls back to a
    // driven (reference) dimension if it would over-constrain the sketch.
    int addDimension(cad::SkCon type, int e1, int e2, cad::Vec2 label, bool supplementary = false,
                     bool *driven = nullptr);
    // Offset: copies of the curves `leftDistance` to the left of their chains
    // (negative: to the right), joined at the corners and held there by one
    // offset dimension, as one undo step. Returns the new curves (empty, with
    // `error`, if the offset cannot be made).
    std::vector<int> offsetCurves(const std::vector<int> &curves, double leftDistance, QString *error = nullptr);
    bool setDimensionExpression(int constraintId, const QString &expr, QString *error = nullptr);
    void deleteSelection();
    void toggleConstruction();
    bool undo();
    bool redo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString undoLabel() const { return m_undo.empty() ? QString() : m_undo.back().label; }
    bool modified() const { return m_modified; }

    // Solves the working copy; `dragged` points are kept at their positions.
    void solve(const std::vector<int> &dragged = {}, bool analyse = true);
    // Solves `s` in place with the current parameters (no free-entity analysis).
    cad::SolveOutcome trySolve(cad::Sketch &s) const;
    const cad::SolveOutcome &solveResult() const { return m_solve; }
    // Evaluates a value typed by the user against the model parameters.
    cad::EvalResult evaluate(const std::string &expr, cad::ValueKind kind) const;
    const std::vector<cad::Profile> &profiles() const { return m_profiles; }

    // --- dimensions -------------------------------------------------------------------
    double measuredValue(const cad::SkConstraint &c) const;   // from the geometry
    double dimensionValue(const cad::SkConstraint &c) const;  // parameter value (driving) or measured
    QString dimensionText(const cad::SkConstraint &c) const;  // as displayed on the canvas
    cad::Vec2 dimensionAnchor(const cad::SkConstraint &c) const;
    std::optional<QRectF> dimensionRect(int constraintId) const; // label rectangle (last paint)
    static std::string formatExpression(double value, cad::ValueKind kind);
    static cad::ValueKind kindOf(cad::SkCon type) {
        return type == cad::SkCon::Angle ? cad::ValueKind::Angle : cad::ValueKind::Length;
    }

    // --- picking ------------------------------------------------------------------------
    SketchHit hitTest(QPointF px, unsigned filter = HitDefault) const;
    SketchSnap snap(QPointF px, const std::set<int> &excludePoints = {}, bool allowCurves = true) const;
    // Makes (or reuses) a point for a snap; constraints attaching it go to `pending`.
    int pointForSnap(cad::Sketch &s, const SketchSnap &snap, std::vector<PendingConstraint> &pending) const;
    // Curves and standalone points inside (window) or touching (crossing) a pixel rectangle.
    std::vector<int> entitiesInRect(const QRectF &rect, bool crossing) const;
    // Polyline of a curve in sketch coordinates.
    std::vector<cad::Vec2> polyline(const cad::SkEntity &e) const;

    // --- selection ----------------------------------------------------------------------
    std::set<int> selectedEntities;    // may include kSketchOrigin / axes
    std::set<int> selectedConstraints;
    std::set<int> selectedProfiles;    // profile indices
    SketchHit hover;
    void clearSelection();
    void select(const SketchHit &hit, bool additive);
    void selectEntities(const std::vector<int> &ids, bool additive);
    // Curves connected end to end with `curveId`.
    std::vector<int> connectedChain(int curveId) const;
    bool isSelected(const SketchHit &hit) const;
    QString selectionStats() const;

    // --- dragging (select tool) ---------------------------------------------------------
    bool beginDrag(const SketchHit &hit, QPointF px);
    void dragTo(QPointF px);
    void endDrag();
    // Esc: puts everything back where it was before the drag.
    void cancelDrag();
    bool dragging() const { return m_drag.active; }

    // --- drawing -----------------------------------------------------------------------
    void contribute(RenderScene &scene) const;
    void paintOverlay(QPainter &p);
    // Draws a dimension that is not in the sketch yet (the Dimension tool's preview).
    void paintDimensionPreview(QPainter &p, const cad::SkConstraint &c);
    // Temporary geometry drawn by the active tool (sketch coordinates).
    std::vector<std::pair<cad::Vec2, cad::Vec2>> previewLines;
    std::vector<std::pair<cad::Vec2, cad::Vec2>> previewConstruction;
    std::vector<cad::Vec2> previewPoints;
    std::optional<SketchSnap> previewSnap;
    void clearPreview();
    // Repaints the viewport and its overlay.
    void refreshView() const;

signals:
    void changed();              // geometry, constraints or selection changed
    void message(const QString &text);

private:
    struct Glyph {
        int constraint;
        cad::Vec2 anchor;
        cad::Vec2 side; // offset direction (sketch units, unit length)
    };

    void rebuildProfiles();
    void emitChanged();
    void pruneSelection();
    std::vector<Glyph> glyphs() const;
    void paintDimension(QPainter &p, const cad::SkConstraint &c);
    void paintGlyphs(QPainter &p);
    bool lineEnds(int lineId, cad::Vec2 &a, cad::Vec2 &b) const;
    bool curveNear(const cad::SkEntity &e, QPointF px, double tol, double *dist) const;

    Viewport *m_viewport;
    std::shared_ptr<cad::SketchFeature> m_feature;
    gp_Ax3 m_frame;
    ParamProvider m_paramProvider;
    ParamAllocator m_allocate;
    std::shared_ptr<cad::ParamTable> m_params;
    SketchDisplayOptions m_options;
    bool m_modified = false;

    cad::SolveOutcome m_solve;
    std::vector<cad::Profile> m_profiles;
    std::vector<std::vector<QVector3D>> m_profileTriangles;

    struct Snapshot {
        QString label;
        cad::Sketch sketch;
    };
    std::vector<Snapshot> m_undo, m_redo;

    struct Drag {
        bool active = false;
        SketchHit hit;
        cad::Vec2 start;
        cad::Sketch before;
        cad::Sketch lastGood;
        std::map<int, cad::Vec2> origin; // point id -> start position
        cad::Vec2 labelStart;
        bool moved = false;
        bool rigid = false; // a selection moved as a whole (circles keep their size)
    } m_drag;

    // Screen rectangles of dimension labels and constraint glyphs (last paint).
    std::map<int, QRectF> m_dimensionRects;
    std::vector<std::pair<int, QRectF>> m_glyphRects;
};

} // namespace cadly
