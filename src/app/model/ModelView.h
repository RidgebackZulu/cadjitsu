#pragma once

#include "model/PlaneDisplay.h"
#include "model/RecomputeService.h"
#include "model/Selection.h"
#include "viewport/Picker.h"
#include "viewport/RenderScene.h"

#include "doc/Document.h"
#include "sketch/SketchResult.h"

#include <QColor>
#include <QObject>
#include <QString>

#include <gp_Pnt.hxx>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace cadly {

class Viewport;

// What canvas clicks may pick. Commands narrow it while one of their inputs
// is active; with `toggle` plain clicks add and remove (as in Fusion's
// command inputs) instead of replacing the selection.
struct SelectFilter {
    bool faces = true;
    bool edges = true;
    bool vertices = true;
    bool bodies = true;          // double-click and box selection pick bodies
    bool profiles = true;        // sketch regions
    bool planes = false;         // construction planes, and the origin planes when shown
    bool sketchPoints = false;   // points of the sketches on screen
    bool planarFacesOnly = false;
    bool linearEdgesOnly = false;
    bool faceSelectsBody = false; // a click on a body picks the whole body
    bool toggle = false;

    // Outside commands everything on screen can be picked, planes included.
    static SelectFilter idle() {
        SelectFilter f;
        f.planes = true;
        return f;
    }
};

// Presents a model evaluation in a Viewport: builds render content (bodies,
// construction planes, visible sketches with shaded profiles, the origin),
// and implements hover, click / box selection of faces, edges, vertices,
// bodies, sketch profiles and points and planes, plus the selection
// statistics shown at the bottom right of the window.
class ModelView : public QObject {
    Q_OBJECT

public:
    ModelView(cad::Document &doc, Viewport *viewport, QObject *parent = nullptr);

    cad::Document &document() { return m_doc; }
    cad::StatePtr state() const { return m_state; }
    EvaluationPtr evaluation() const { return m_eval; }
    Viewport *viewport() const { return m_viewport; }

    // Shows an evaluation (normally delivered by the RecomputeService).
    void setEvaluation(EvaluationPtr e);
    // Evaluates the document on this thread and shows it (programmatic edits).
    void showDocumentNow();
    // Rebuilds the scene from the current evaluation (visibility changes).
    void refresh();

    const SelectionSet &selection() const { return m_selection; }
    void setSelection(const SelectionSet &s);
    void clearSelection();
    QString statsText() const { return m_stats; }

    void setFilter(const SelectFilter &f);
    const SelectFilter &filter() const { return m_filter; }
    // Shorthand for the common filters (the rest of the filter is reset).
    void setSelectable(bool faces, bool edges, bool vertices, bool bodies, bool profiles = true);
    // What a click at `hit` would pick with the current filter.
    std::optional<SelectionItem> itemAt(const PickHit &hit) const;

    // Inside a command, clicks are reported (picked / markClicked) instead of
    // changing the selection, and the command draws its inputs as marks. Mark
    // edges and points can be clicked (e.g. to drop an edge the previewed
    // fillet has rounded away); tags say which input they show.
    struct InputMarks {
        std::vector<FaceHighlight> faces;
        std::vector<std::pair<int, EdgeHighlight>> edges;
        std::vector<std::pair<int, QVector3D>> points;
        std::vector<TriangleBatch> triangles;
    };
    void setCommandInput(bool on);
    bool commandInput() const { return m_commandInput; }
    void setInputMarks(InputMarks marks);
    const InputMarks &inputMarks() const { return m_marks; }
    // The mark under a canvas position.
    std::optional<int> markAt(QPointF px) const;

    // A sketch drawn by its editor instead (while it is being edited).
    void setHiddenSketch(cad::FeatureId id);
    void setOriginVisible(bool on);
    bool originVisible() const { return m_originVisible; }
    // Shows the origin planes while a command wants a plane picked.
    void setOriginForced(bool on);
    // Sketches kept visible by a running command (its profiles' sketches).
    void setForcedSketches(std::set<cad::FeatureId> ids);
    // Sketches are shown until a displayed feature uses them, unless the user
    // switched them on or off.
    bool sketchShown(cad::FeatureId id) const;

    // The sketch profile under a canvas position, if any (nearest along the ray).
    struct ProfilePick {
        cad::FeatureId sketch = cad::kNoFeature;
        std::string key;
        cad::Vec2 sample;
        float rayT = 0.0f;
    };
    std::optional<ProfilePick> pickProfile(QPointF px) const;
    const cad::Profile *profileOf(const SelectionItem &it) const;
    // The plane a Plane item or a planar Face item stands for.
    std::optional<cad::PlaneRef> planeRefOf(const SelectionItem &it) const;
    // Where a sketch point item is.
    std::optional<gp_Pnt> sketchPointOf(const SelectionItem &it) const;
    // The pickable planes as drawn.
    const std::vector<std::pair<SelectionItem, Quad>> &planeQuads() const { return m_planeQuads; }
    // A profile's shading triangles.
    std::vector<QVector3D> profileTrianglesOf(cad::FeatureId sketch, const std::string &key);

    static QColor defaultBodyColor();

signals:
    // Command input mode: the user clicked something (or empty space) / a mark.
    void picked(const std::optional<cadly::SelectionItem> &item, const cadly::PickHit &hit, Qt::KeyboardModifiers modifiers);
    void markClicked(int tag);
    void selectionChanged();
    // Only for picks made by the user in the canvas (not programmatic changes).
    void userSelectionChanged();
    void statsChanged(const QString &text);
    // A new evaluation is on screen.
    void displayed();

private:
    void onHoverMoved(const PickHit &hit);
    void onClicked(const PickHit &hit, Qt::KeyboardModifiers mods);
    void onDoubleClicked(const PickHit &hit);
    void onBoxSelected(const QRectF &rect, bool crossing, Qt::KeyboardModifiers mods);
    bool accepts(const PickHit &hit) const;
    std::optional<std::pair<SelectionItem, float>> pickPlane(QPointF px) const;
    std::optional<SelectionItem> pickSketchPoint(QPointF px) const;
    bool originPlanesShown() const { return m_originVisible || m_originForced; }
    void updateHighlights();
    void updateStats();
    void pruneSelection();
    const cad::Body *bodyById(const cad::BodyId &id) const;
    const std::vector<std::vector<QVector3D>> &profileTriangles(const std::shared_ptr<const cad::SketchResult> &sk);
    std::set<cad::FeatureId> consumedSketches() const;

    cad::Document &m_doc;
    Viewport *m_viewport;
    EvaluationPtr m_eval;
    cad::StatePtr m_state;
    SelectionSet m_selection;
    std::optional<SelectionItem> m_hover;
    std::optional<int> m_markHover;
    bool m_commandInput = false;
    InputMarks m_marks;
    QString m_stats;
    SelectFilter m_filter;
    bool m_originVisible = false;
    bool m_originForced = false;
    cad::FeatureId m_hiddenSketch = cad::kNoFeature;
    std::set<cad::FeatureId> m_forcedSketches;
    std::set<cad::FeatureId> m_shownSketches; // drawn in the current scene
    std::vector<std::pair<SelectionItem, Quad>> m_planeQuads; // planes drawn in the current scene
    // Profile shading per evaluated sketch (one triangle list per profile).
    std::map<std::shared_ptr<const cad::SketchResult>, std::vector<std::vector<QVector3D>>> m_profileCache;
};

} // namespace cadly
