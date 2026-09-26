#pragma once

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

class ModelView;
class Ribbon;
class RibbonTab;
class SketchMode;
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
    Ribbon *ribbon() const { return m_ribbon; }
    RibbonTab *solidTab() const { return m_solidTab; }
    RibbonTab *sketchTab() const { return m_sketchTab; }
    cad::Document &document() { return *m_document; }
    QLabel *selectionStatsLabel() const { return m_selectionStats; }
    // Actions by object name ("createSketch", "sketchLine", "finishSketch", "undo"...).
    QAction *action(const QString &name) const;

    // Re-reads the document into the canvas (after programmatic edits).
    void refresh();

    bool openFile(const QString &path);
    bool saveFile(const QString &path);
    void newDocument();
    void undo();
    void redo();

private:
    QAction *makeAction(const char *name, const QString &text, IconId icon, const QKeySequence &shortcut,
                        std::function<void()> fn);
    void buildActions();
    void buildRibbon();
    void buildMenus();
    void updateTitle();
    void updateStats();
    void onSketchActive(bool active);
    void onSketchTool(SketchToolKind kind);
    void showCanvasMenu(const QPoint &globalPos);

    std::unique_ptr<cad::Document> m_document;
    Viewport *m_viewport = nullptr;
    ModelView *m_modelView = nullptr;
    SketchMode *m_sketch = nullptr;
    Ribbon *m_ribbon = nullptr;
    RibbonTab *m_solidTab = nullptr;
    RibbonTab *m_sketchTab = nullptr;
    QLabel *m_selectionStats = nullptr;
    QString m_path;
    std::map<QString, QAction *> m_actions;
    std::map<SketchToolKind, QAction *> m_toolActions;
    QActionGroup *m_toolGroup = nullptr;
    std::vector<QAction *> m_sketchOnly;
};

} // namespace cadly
