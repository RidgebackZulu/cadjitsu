#pragma once

#include "model/RecomputeService.h"
#include "sketch/SketchTools.h"

#include "doc/Document.h"

#include <QMainWindow>

#include <functional>
#include <map>
#include <memory>

class QAction;
class QActionGroup;
class QLabel;

namespace cadly {

class BrowserTree;
class Command;
class CommandController;
class CommandPanel;
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
    void showMarkingMenu(QPoint canvasPos);

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
    void finishInteractions();

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
    QString m_path;
    QString m_lastCommand;
    std::map<QString, QAction *> m_actions;
    std::map<SketchToolKind, QAction *> m_toolActions;
    QActionGroup *m_toolGroup = nullptr;
    std::vector<QAction *> m_sketchOnly;
};

} // namespace cadly
