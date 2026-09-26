#pragma once

#include "model/Selection.h"
#include "viewport/Picker.h"
#include "viewport/RenderScene.h"

#include "doc/Document.h"
#include "sketch/SketchResult.h"

#include <map>
#include <memory>
#include <vector>

#include <QColor>
#include <QObject>
#include <QString>

namespace cadly {

class Viewport;

// Presents a Document in a Viewport: builds render content from the displayed
// model state, and implements hover, click / box selection and the
// selection statistics shown at the bottom right of the window.
class ModelView : public QObject {
    Q_OBJECT

public:
    ModelView(cad::Document &doc, Viewport *viewport, QObject *parent = nullptr);

    cad::Document &document() { return m_doc; }
    cad::StatePtr state() const { return m_state; }
    Viewport *viewport() const { return m_viewport; }

    // Pulls the displayed state from the document and updates the viewport.
    void refresh();

    const SelectionSet &selection() const { return m_selection; }
    void setSelection(const SelectionSet &s);
    void clearSelection();
    QString statsText() const { return m_stats; }

    // What canvas clicks may select (commands restrict this, e.g. Fillet -> edges).
    void setSelectable(bool faces, bool edges, bool vertices, bool bodies);
    // A sketch drawn by its editor instead (while it is being edited).
    void setHiddenSketch(cad::FeatureId id) { m_hiddenSketch = id; }

    static QColor defaultBodyColor();

signals:
    void selectionChanged();
    void statsChanged(const QString &text);

private:
    void onHover(const PickHit &hit);
    void onClicked(const PickHit &hit, Qt::KeyboardModifiers mods);
    void onDoubleClicked(const PickHit &hit);
    void onBoxSelected(const QRectF &rect, bool crossing, Qt::KeyboardModifiers mods);
    void updateHighlights();
    void updateStats();
    void pruneSelection();
    const cad::Body *bodyById(const cad::BodyId &id) const;

    cad::Document &m_doc;
    Viewport *m_viewport;
    cad::StatePtr m_state;
    SelectionSet m_selection;
    PickHit m_hover;
    QString m_stats;
    bool m_selectBodies = true;
    cad::FeatureId m_hiddenSketch = cad::kNoFeature;
    // Profile shading of visible sketches, per evaluated sketch.
    std::map<std::shared_ptr<const cad::SketchResult>, std::vector<QVector3D>> m_profileCache;
};

} // namespace cadly
