#pragma once

#include "io/Exporter.h"
#include "model/RecomputeService.h"
#include "sketch/SketchTools.h"

#include "doc/Document.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>

#include <functional>
#include <map>
#include <memory>
#include <vector>

class QAction;
class QActionGroup;
class QLabel;
class QMenu;

namespace cadjitsu {

class BrowserTree;
class Command;
class CommandController;
class CanvasValueBox;
class DistanceManipulator;
class ValueField;
class CommandPanel;
class ExportDialog;
class McpButton;
class McpDialog;
class McpLog;
class McpServer;
class McpTools;
class RenderDialog;
class SettingsDialog;
class MarkingMenu;
class ModelView;
class Ribbon;
class RibbonTab;
class SketchMode;
class TimelineWidget;
class Viewport;
enum class IconId;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // Bodies follow the open sketch while it is edited (a setting; default on).
    static bool liveSketchBodies();
    static void setLiveSketchBodies(bool on);
    // The last live update was slow, so updates wait for the mouse to be released.
    bool liveSketchSlow() const { return m_liveSlow; }

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    Viewport *viewport() const { return m_viewport; }
    ModelView *modelView() const { return m_modelView; }
    SketchMode *sketchMode() const { return m_sketch; }
    CommandController *commands() const { return m_commands; }
    CommandPanel *commandPanel() const { return m_commandPanel; }
    RecomputeService *recompute() const { return m_recompute; }
    TimelineWidget *timeline() const { return m_timeline; }
    BrowserTree *browser() const { return m_browser; }
    MarkingMenu *markingMenu() const { return m_marking; }
    Ribbon *ribbon() const { return m_ribbon; }
    RibbonTab *solidTab() const { return m_solidTab; }
    RibbonTab *sketchTab() const { return m_sketchTab; }
    cad::Document &document() { return *m_document; }
    QLabel *selectionStatsLabel() const { return m_selectionStats; }
    // Actions by object name ("createSketch", "extrude", "sketchLine", "undo"...).
    QAction *action(const QString &name) const;

    // Evaluates the document right now and shows it (after programmatic edits).
    void refresh();
    // Waits until the background recompute has delivered the latest model.
    bool waitForModel(int timeoutMs = 60000);

    bool openFile(const QString &path);
    bool saveFile(const QString &path);
    void newDocument();
    void undo();
    void redo();
    void startExtrude();
    // Starts a modelling command (the marking menu's Repeat remembers `name`).
    void startCommand(const QString &name, std::unique_ptr<Command> cmd);
    void editFeature(cad::FeatureId id);
    void editSection(int id);
    // Canvases: insert a picture file (asks for the plane and size in the
    // panel), place one again, calibrate it, correct its perspective.
    bool insertCanvas(const QString &path);
    void editCanvas(int id);
    void calibrateCanvas(int id);
    void correctCanvasPerspective(int id);
    // The shown section's depth arrow on the canvas (always there to drag).
    DistanceManipulator *sectionArrow() const { return m_sectionArrow.get(); }
    // The shown section's depth, typed or following the arrow (on the canvas).
    CanvasValueBox *sectionDepthBox() const { return m_sectionBox; }
    ValueField *sectionDepthField() const { return m_sectionDepth; }
    void showMarkingMenu(QPoint canvasPos);
    // File > Export and MAKE > 3D Print: opens the export dialog (window-modal,
    // deleted when closed) for the model as it is now.
    ExportDialog *openExportDialog(ExportJob::Format format);
    // Cadjitsu > Settings (mouse bindings).
    SettingsDialog *openSettings();
    // The MCP server (AI agents), its event log, status button and dialog.
    McpServer *mcpServer() const { return m_mcp; }
    McpLog *mcpLog() const { return m_mcpLog; }
    McpButton *mcpButton() const { return m_mcpButton; }
    McpTools *mcpTools() const { return m_mcpTools.get(); }
    McpDialog *openMcpDialog();
    // Deletes a sketch (finishing it first if it is open). With `ask`, asks
    // first when other features use it. False if not deleted.
    bool deleteSketch(cad::FeatureId id, bool ask = true);
    // Right-click on sketch geometry: Construction / Normal, Mirror, Pattern,
    // Delete. False if there is no sketch geometry there.
    bool showSketchEntityMenu(const QPoint &globalPos);
    QMenu *sketchEntityMenu() const { return m_sketchEntityMenu; } // while open (tests)
    // The Render dialog (materials, plate, lighting), for these bodies (all if none).
    RenderDialog *openRenderDialog(const std::vector<cad::BodyId> &bodies = {});
    // Ends an open sketch or command (and closes menus) before other edits.
    void finishInteractions();

private:
    QAction *makeAction(const char *name, const QString &text, IconId icon, const QKeySequence &shortcut,
                        std::function<void()> fn);
    void buildActions();
    void buildRibbon();
    void buildMenus();
    void updateTitle();
    void updateStats();
    void updateActions();
    void onSketchActive(bool active);
    void onSketchTool(SketchToolKind kind);
    void onDocumentChanged();
    void onEvaluation(const EvaluationPtr &e);
    void updateSectionArrow(bool resetBox = false);
    void placeSectionBox();
    // A section depth typed in the box: previewed, or kept (Enter).
    void previewSectionDepth();
    void applySectionDepth();
    // Bodies built from the open sketch follow its edits (a background what-if
    // evaluation with the edited sketch in the timeline).
    void requestLiveSketch();

    std::unique_ptr<cad::Document> m_document;
    Viewport *m_viewport = nullptr;
    ModelView *m_modelView = nullptr;
    SketchMode *m_sketch = nullptr;
    RecomputeService *m_recompute = nullptr;
    CommandPanel *m_commandPanel = nullptr;
    CommandController *m_commands = nullptr;
    TimelineWidget *m_timeline = nullptr;
    BrowserTree *m_browser = nullptr;
    MarkingMenu *m_marking = nullptr;
    Ribbon *m_ribbon = nullptr;
    RibbonTab *m_solidTab = nullptr;
    RibbonTab *m_sketchTab = nullptr;
    QLabel *m_selectionStats = nullptr;
    QLabel *m_busy = nullptr;
    std::unique_ptr<DistanceManipulator> m_sectionArrow;
    ValueField *m_sectionDepth = nullptr;
    CanvasValueBox *m_sectionBox = nullptr;
    McpLog *m_mcpLog = nullptr;
    std::unique_ptr<McpTools> m_mcpTools;
    McpServer *m_mcp = nullptr;
    McpButton *m_mcpButton = nullptr;
    QPointer<McpDialog> m_mcpDialog;
    QPointer<RenderDialog> m_renderDialog;
    QMenu *m_sketchEntityMenu = nullptr;
    QString m_path;
    QString m_lastCommand;
    std::map<QString, QAction *> m_actions;
    std::map<SketchToolKind, QAction *> m_toolActions;
    QActionGroup *m_toolGroup = nullptr;
    std::vector<QAction *> m_sketchOnly;
    // Live sketch updates.
    uint64_t m_liveRequest = 0;
    bool m_liveInFlight = false, m_livePending = false, m_liveShown = false, m_liveSlow = false;
    std::string m_liveSketchJson;
    QElapsedTimer m_liveClock;
};

} // namespace cadjitsu
