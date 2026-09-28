#pragma once

#include "sketch/HeadsUpInput.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchTools.h"

#include "doc/Document.h"
#include "topo/Refs.h"

#include <QObject>
#include <QPointer>

#include <memory>

namespace cadjitsu {

class CommandPanel;
class ModelView;
class PlanePickTool;
class SketchPalette;
class Viewport;

// The sketch workspace: picking a plane for a new sketch, opening a sketch,
// the active sketch tool, the sketch palette and heads-up input, the sketch's
// own undo history, and committing it into the document (Finish Sketch).
// A new sketch appears in the timeline as soon as it is started; creating and
// filling it in is a single undo step of the document.
class SketchMode : public QObject {
    Q_OBJECT

public:
    SketchMode(cad::Document &doc, Viewport *viewport, ModelView *modelView, QObject *parent = nullptr);
    ~SketchMode() override;

    bool active() const { return m_editor != nullptr; }
    SketchEditor *editor() const { return m_editor.get(); }
    HeadsUpInput &hud() const { return *m_hud; }
    SketchPalette *palette() const { return m_palette; }
    Viewport *viewport() const { return m_viewport; }
    ModelView *modelView() const { return m_modelView; }
    cad::Document &document() { return m_doc; }
    // The panel tools with settings (Text) show them in.
    void setCommandPanel(CommandPanel *panel);
    CommandPanel *commandPanel() const;

    // "Create Sketch": the origin planes appear and a plane or planar face is picked.
    void startCreateSketch();
    bool pickingPlane() const { return m_planePick != nullptr; }
    void cancelCreateSketch();
    // Starts a new sketch on a plane (adds the sketch feature at the history marker).
    bool beginNewSketch(const cad::PlaneRef &plane, bool animate = true);
    // Opens an existing sketch for editing.
    bool editSketch(cad::FeatureId id, bool animate = true);
    // Commits the working sketch into the document and leaves sketch mode.
    void finish();

    void setTool(SketchToolKind kind);
    SketchToolKind tool() const { return m_toolKind; }
    SketchTool *currentTool() const { return m_tool.get(); }
    // Opens the inline value box on a dimension's label.
    void editDimension(int constraintId);
    // Opens a line's length, a circle's diameter or an arc's radius for
    // typing: its dimension if it has one, else a new one beside it.
    bool editSize(int entityId);
    // Opens a text in the Text tool's panel.
    bool editText(int entityId);
    InlineValueEditor *dimensionEditor() const { return m_dimensionEdit; }
    bool undo();
    bool redo();
    void lookAt(bool animate = true);
    void deleteSelection();
    void toggleConstruction();
    void showStatus(const QString &text);
    // Ends what the current tool is doing (a line chain, a rectangle in
    // progress); false if it was idle. Right-click does this first.
    bool cancelOperation();

signals:
    void activeChanged(bool active);
    void pickingPlaneChanged(bool picking);
    void toolChanged(cadjitsu::SketchToolKind tool);
    void statsChanged(const QString &text);
    void message(const QString &text);
    void finished(cad::FeatureId id);
    // The open sketch's geometry changed (its downstream bodies can follow).
    void geometryChanged();

private:
    bool enter(cad::FeatureId id, bool isNew, bool animate);
    void leave();
    void closeDimensionEditor();
    // Tools and the plane picker are deleted from the event loop, never while
    // one of their own handlers is running.
    void retire(std::unique_ptr<SketchTool> tool);
    void retire(std::unique_ptr<PlanePickTool> tool);

    cad::Document &m_doc;
    QPointer<Viewport> m_viewport;
    ModelView *m_modelView;
    std::unique_ptr<SketchEditor> m_editor;
    std::unique_ptr<HeadsUpInput> m_hud;
    std::unique_ptr<SketchTool> m_tool;
    std::unique_ptr<PlanePickTool> m_planePick;
    SketchToolKind m_toolKind = SketchToolKind::Select;
    SketchPalette *m_palette = nullptr;
    QPointer<CommandPanel> m_commandPanel;
    QPointer<InlineValueEditor> m_dimensionEdit;
    std::vector<std::unique_ptr<SketchTool>> m_retiredTools;
    std::vector<std::unique_ptr<PlanePickTool>> m_retiredPicks;
    bool m_isNew = false;
};

} // namespace cadjitsu
