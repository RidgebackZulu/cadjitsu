#pragma once

#include "model/RecomputeService.h"
#include "model/Selection.h"
#include "viewport/Picker.h"
#include "viewport/RenderScene.h"

#include "doc/Document.h"
#include "sketch/SketchResult.h"

#include <QColor>
#include <QObject>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace cadly {

class Viewport;

// Presents a model evaluation in a Viewport: builds render content (bodies,
// construction planes, visible sketches with shaded profiles, the origin),
// and implements hover, click / box selection of faces, edges, vertices,
// bodies and sketch profiles, plus the selection statistics shown at the
// bottom right of the window.
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

    // What canvas clicks may select (commands restrict this, e.g. Fillet -> edges).
    void setSelectable(bool faces, bool edges, bool vertices, bool bodies, bool profiles = true);
    // A sketch drawn by its editor instead (while it is being edited).
    void setHiddenSketch(cad::FeatureId id);
    void setOriginVisible(bool on);
    bool originVisible() const { return m_originVisible; }
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

    static QColor defaultBodyColor();

signals:
    void selectionChanged();
    // Only for picks made by the user in the canvas (not programmatic changes).
    void userSelectionChanged();
    void statsChanged(const QString &text);
    // A new evaluation is on screen.
    void displayed();

private:
    void onHover(const PickHit &hit);
    void onHoverMoved(const PickHit &hit);
    void onClicked(const PickHit &hit, Qt::KeyboardModifiers mods);
    void onDoubleClicked(const PickHit &hit);
    void onBoxSelected(const QRectF &rect, bool crossing, Qt::KeyboardModifiers mods);
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
    PickHit m_hover;
    std::optional<ProfilePick> m_profileHover;
    QString m_stats;
    bool m_selectBodies = true;
    bool m_selectProfiles = true;
    bool m_originVisible = false;
    cad::FeatureId m_hiddenSketch = cad::kNoFeature;
    std::set<cad::FeatureId> m_forcedSketches;
    std::set<cad::FeatureId> m_shownSketches; // drawn in the current scene
    // Profile shading per evaluated sketch (one triangle list per profile).
    std::map<std::shared_ptr<const cad::SketchResult>, std::vector<std::vector<QVector3D>>> m_profileCache;
};

} // namespace cadly
