#include "MainWindow.h"

#include "command/Command.h"
#include "command/CommandPanel.h"
#include "command/CombineCommand.h"
#include "command/EdgeCommands.h"
#include "command/ExtrudeCommand.h"
#include "command/HoleCommand.h"
#include "command/PlaneCommand.h"
#include "command/SectionCommand.h"
#include "mcp/McpButton.h"
#include "mcp/McpDialog.h"
#include "mcp/McpLog.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "ui/BrowserTree.h"
#include "ui/ExportDialog.h"
#include "ui/Icons.h"
#include "ui/SettingsDialog.h"
#include "ui/MarkingMenu.h"
#include "ui/Ribbon.h"
#include "ui/TimelineWidget.h"
#include "viewport/Viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace cadly {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_document(std::make_unique<cad::Document>()) {
    resize(1400, 900);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_ribbon = new Ribbon(central);
    auto *split = new QSplitter(Qt::Horizontal, central);
    split->setObjectName(QStringLiteral("mainSplit"));
    split->setChildrenCollapsible(true);
    m_viewport = new Viewport(split);
    m_modelView = new ModelView(*m_document, m_viewport, this);
    m_browser = new BrowserTree(*m_document, m_modelView, split);
    split->addWidget(m_browser);
    split->addWidget(m_viewport);
    split->setStretchFactor(1, 1);
    split->setSizes({210, 1190});
    m_timeline = new TimelineWidget(*m_document, central);
    layout->addWidget(m_ribbon);
    layout->addWidget(split, 1);
    layout->addWidget(m_timeline);
    setCentralWidget(central);

    m_recompute = new RecomputeService(m_document->sharedCache(), this);
    m_sketch = new SketchMode(*m_document, m_viewport, m_modelView, this);
    m_commandPanel = new CommandPanel(m_viewport);
    m_commands = new CommandController({m_document.get(), m_modelView, m_viewport, m_commandPanel}, m_recompute, this);
    m_marking = new MarkingMenu(m_viewport);
    // The shown section's depth can be dragged at any time (Fusion's section
    // arrow, kept on the canvas): live while dragging, one undo step on release.
    m_sectionArrow = std::make_unique<DistanceManipulator>(m_viewport);
    m_sectionArrow->onDrag = [this](double d) {
        if(const cad::SectionAnalysis *s = m_document->activeSection()) {
            cad::SectionAnalysis moved = *s;
            moved.offset = d;
            m_modelView->setSectionOverride(std::optional<cad::SectionAnalysis>(moved));
            m_sectionArrow->label = QString::fromStdString(SketchEditor::formatExpression(d, cad::ValueKind::Length));
        }
    };
    m_sectionArrow->onRelease = [this] {
        m_modelView->setSectionOverride(std::nullopt);
        if(const cad::SectionAnalysis *s = m_document->activeSection()) {
            cad::SectionAnalysis moved = *s;
            moved.offset = m_sectionArrow->distance();
            if(moved.offset != s->offset) m_document->updateSection(moved);
        }
    };
    m_viewport->setIdleTool(m_sectionArrow.get());
    m_viewport->setMouseBindings(MouseBindings::load());

    // Bottom-right selection statistics, as in Fusion 360; a busy note to their left.
    m_busy = new QLabel(this);
    m_busy->setObjectName(QStringLiteral("computing"));
    m_selectionStats = new QLabel(this);
    m_selectionStats->setObjectName(QStringLiteral("selectionStats"));
    m_selectionStats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(m_busy);
    statusBar()->addPermanentWidget(m_selectionStats);

    connect(m_modelView, &ModelView::statsChanged, this, &MainWindow::updateStats);
    connect(m_sketch, &SketchMode::statsChanged, this, &MainWindow::updateStats);
    connect(m_sketch, &SketchMode::message, this, [this](const QString &text) { statusBar()->showMessage(text, 8000); });
    connect(m_sketch, &SketchMode::activeChanged, this, &MainWindow::onSketchActive);
    connect(m_sketch, &SketchMode::toolChanged, this, &MainWindow::onSketchTool);
    connect(m_viewport, &Viewport::contextMenuRequested, this, [this](const QPoint &globalPos, const PickHit &) {
        if(m_sketch->pickingPlane()) return;
        if(m_sketch->active() && m_sketch->cancelOperation()) return; // right-click ends a line chain first
        showMarkingMenu(m_viewport->mapFromGlobal(globalPos));
    });
    connect(m_recompute, &RecomputeService::finished, this, &MainWindow::onEvaluation);
    connect(m_recompute, &RecomputeService::busyChanged, this,
            [this](bool busy) { m_busy->setText(busy ? tr("Computing…") : QString()); });
    connect(m_commands, &CommandController::activeChanged, this, [this](bool on) {
        if(!on) m_recompute->requestDocument(*m_document);
        if(on && m_commands->command()) statusBar()->showMessage(m_commands->command()->prompt());
        else statusBar()->clearMessage();
        updateActions();
    });
    connect(m_commands, &CommandController::editingChanged, m_timeline, &TimelineWidget::setEditing);
    connect(m_timeline, &TimelineWidget::editRequested, this, &MainWindow::editFeature);
    connect(m_browser, &BrowserTree::editSketchRequested, this, [this](cad::FeatureId id) { editFeature(id); });
    connect(m_modelView, &ModelView::editSketchRequested, this, [this](cad::FeatureId id) {
        // Deferred: the double-click is still being delivered to the canvas.
        QTimer::singleShot(0, this, [this, id] { editFeature(id); });
    });
    connect(m_browser, &BrowserTree::editSectionRequested, this, &MainWindow::editSection);
    m_document->changed = [this] { onDocumentChanged(); };

    // AI agents over MCP (off unless switched on in the MCP dialog).
    m_mcpLog = new McpLog(this);
    m_mcpTools = std::make_unique<McpTools>(*this);
    m_mcp = new McpServer(m_mcpLog, m_mcpTools.get(), this);

    buildActions();
    buildRibbon();
    buildMenus();
    onSketchActive(false);
    updateTitle();
    refresh();
    connect(m_mcp, &McpServer::stateChanged, this, [this](McpServer::State s) {
        m_mcpButton->setState(s, m_mcp->clientName());
        if(s == McpServer::State::Connected)
            statusBar()->showMessage(tr("An AI agent connected over MCP: %1").arg(m_mcp->clientName()), 6000);
    });
    connect(m_mcp, &McpServer::activity, this, [this] { m_mcpButton->pulse(); });
    m_mcp->apply(McpSettings::load());
}

MainWindow::~MainWindow() {
    m_mcp->stop();
    m_document->changed = nullptr;
    m_viewport->setIdleTool(nullptr);
    // These use the viewport; take them down while it exists.
    delete m_commands;
    m_commands = nullptr;
    delete m_sketch;
    m_sketch = nullptr;
    delete m_recompute;
    m_recompute = nullptr;
}

QAction *MainWindow::action(const QString &name) const {
    auto it = m_actions.find(name);
    return it == m_actions.end() ? nullptr : it->second;
}

QAction *MainWindow::makeAction(const char *name, const QString &text, IconId iconId, const QKeySequence &shortcut,
                                std::function<void()> fn) {
    auto *a = new QAction(icon(iconId), text, this);
    a->setObjectName(QString::fromLatin1(name));
    if(!shortcut.isEmpty()) {
        a->setShortcut(shortcut);
        a->setToolTip(QStringLiteral("%1 (%2)").arg(text, shortcut.toString(QKeySequence::NativeText)));
    }
    if(fn) connect(a, &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
    addAction(a); // shortcuts work whichever ribbon tab is showing
    m_actions[a->objectName()] = a;
    return a;
}

// --- model updates ------------------------------------------------------------------

void MainWindow::refresh() {
    m_modelView->showDocumentNow();
    m_recompute->supersedeRequests();
    m_timeline->refresh();
    m_timeline->setEvaluation(m_modelView->evaluation());
    m_browser->rebuild();
    updateStats();
    updateActions();
}

bool MainWindow::waitForModel(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    // A command may still have its coalesced preview request queued.
    auto waiting = [this] { return m_commands->previewPending() || m_recompute->busy(); };
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    } while(waiting() && t.elapsed() <= timeoutMs);
    // Let the delivered result reach the canvas.
    QCoreApplication::processEvents();
    return !waiting();
}

void MainWindow::onDocumentChanged() {
    m_timeline->refresh();
    if(!m_commands->active()) m_recompute->requestDocument(*m_document);
    // View changes (sections, visibility) show at once, even while a command is
    // open. (The browser may be inside a click on an item it would replace.)
    m_modelView->refresh();
    QMetaObject::invokeMethod(m_browser, &BrowserTree::rebuild, Qt::QueuedConnection);
    updateSectionArrow();
    updateActions();
}

void MainWindow::updateSectionArrow() {
    const cad::SectionAnalysis *s = m_document->activeSection();
    const cad::StatePtr st = m_modelView->state();
    QVector3D o, d;
    const bool show = s && st && SectionCommand::arrowAxis(*st, s->plane, o, d);
    m_sectionArrow->setVisible(show);
    if(show) {
        m_sectionArrow->setAxis(o, d);
        if(!m_sectionArrow->dragging()) {
            m_sectionArrow->setDistance(s->offset);
            m_sectionArrow->label = QString::fromStdString(SketchEditor::formatExpression(s->offset, cad::ValueKind::Length));
        }
    }
    m_viewport->refreshOverlay();
}

void MainWindow::onEvaluation(const EvaluationPtr &e) {
    if(!m_commands->accepts(e)) return;
    m_modelView->setEvaluation(e);
    m_commands->onEvaluation(e);
    if(!e->preview) m_timeline->setEvaluation(e);
    m_browser->rebuild();
    updateSectionArrow();
    updateStats();
}

void MainWindow::updateTitle() {
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName();
    setWindowTitle(name + QStringLiteral(" — Cadly"));
    m_browser->setDocumentName(name);
}

void MainWindow::updateStats() {
    if(m_sketch->active()) m_selectionStats->setText(m_sketch->editor()->selectionStats());
    else m_selectionStats->setText(m_modelView->statsText());
}

void MainWindow::updateActions() {
    if(!m_commands || m_actions.empty()) return;
    const bool sketching = m_sketch->active(), commanding = m_commands->active();
    action(QStringLiteral("createSketch"))->setEnabled(!sketching);
    for(const char *name : {"extrude", "hole", "fillet", "chamfer", "combine", "offsetPlane", "sectionAnalysis"})
        action(QString::fromLatin1(name))->setEnabled(!commanding);
    action(QStringLiteral("undo"))->setEnabled(sketching || commanding || m_document->canUndo());
    action(QStringLiteral("redo"))->setEnabled(sketching || m_document->canRedo());
}

// Ends a sketch or command before document-level actions.
void MainWindow::finishInteractions() {
    if(m_commands->active()) m_commands->cancel();
    if(m_sketch->active()) m_sketch->finish();
    m_sketch->cancelCreateSketch();
    if(m_marking->isOpen()) m_marking->close();
}

// --- files and undo -------------------------------------------------------------------

void MainWindow::newDocument() {
    finishInteractions();
    m_document->clear();
    m_path.clear();
    updateTitle();
    refresh();
    m_viewport->setStandardView(StandardView::Home, false);
}

bool MainWindow::openFile(const QString &path) {
    finishInteractions();
    std::string error;
    auto doc = std::make_unique<cad::Document>();
    if(!doc->load(path.toStdString(), error)) {
        QMessageBox::warning(this, tr("Open"), tr("Could not open %1:\n%2").arg(path, QString::fromStdString(error)));
        return false;
    }
    std::string e2;
    m_document->fromJson(doc->toJson(), e2);
    m_path = path;
    updateTitle();
    refresh();
    m_viewport->fitAll(false);
    return true;
}

bool MainWindow::saveFile(const QString &path) {
    if(m_commands->active()) m_commands->commit();
    if(m_sketch->active()) m_sketch->finish();
    std::string error;
    if(!m_document->save(path.toStdString(), error)) {
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n%2").arg(path, QString::fromStdString(error)));
        return false;
    }
    m_path = path;
    updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(path).fileName()), 4000);
    return true;
}

void MainWindow::undo() {
    if(m_sketch->active()) {
        if(!m_sketch->undo()) statusBar()->showMessage(tr("Nothing to undo in this sketch."), 4000);
        return;
    }
    if(m_commands->active()) {
        m_commands->cancel();
        return;
    }
    m_sketch->cancelCreateSketch();
    const std::string label = m_document->undoLabel();
    if(m_document->undo()) statusBar()->showMessage(tr("Undo %1").arg(QString::fromStdString(label)), 4000);
}

void MainWindow::redo() {
    if(m_sketch->active()) {
        m_sketch->redo();
        return;
    }
    if(m_commands->active()) return;
    const std::string label = m_document->redoLabel();
    if(m_document->redo()) statusBar()->showMessage(tr("Redo %1").arg(QString::fromStdString(label)), 4000);
}

// --- commands -----------------------------------------------------------------------------

void MainWindow::startExtrude() {
    auto cmd = std::make_unique<ExtrudeCommand>(CommandContext{m_document.get(), m_modelView, m_viewport, m_commandPanel});
    if(m_sketch->active()) {
        // Fusion finishes the sketch and extrudes its profile (if it has one).
        const SketchEditor *ed = m_sketch->editor();
        if(ed->profiles().size() == 1)
            cmd->setInitialProfiles({{ed->featureId(), ed->profiles().front().key, ed->profiles().front().sample}});
        m_sketch->finish();
        m_modelView->clearSelection();
    }
    m_sketch->cancelCreateSketch();
    m_lastCommand = QStringLiteral("extrude");
    m_commands->start(std::move(cmd));
}

void MainWindow::startCommand(const QString &name, std::unique_ptr<Command> cmd) {
    if(m_sketch->active()) m_sketch->finish(); // as in Fusion, a model command ends the sketch
    m_sketch->cancelCreateSketch();
    m_lastCommand = name;
    m_commands->start(std::move(cmd));
}

void MainWindow::editSection(int id) {
    if(!m_document->section(id)) return;
    finishInteractions();
    m_commands->start(
        std::make_unique<SectionCommand>(CommandContext{m_document.get(), m_modelView, m_viewport, m_commandPanel}, id));
}

void MainWindow::editFeature(cad::FeatureId id) {
    const cad::FeaturePtr f = m_document->feature(id);
    if(!f) return;
    finishInteractions();
    const CommandContext ctx{m_document.get(), m_modelView, m_viewport, m_commandPanel};
    switch(f->type()) {
    case cad::FeatureType::Sketch:
        m_sketch->editSketch(id);
        return;
    case cad::FeatureType::Extrude: m_commands->start(std::make_unique<ExtrudeCommand>(ctx, id)); return;
    case cad::FeatureType::Fillet: m_commands->start(std::make_unique<FilletCommand>(ctx, id)); return;
    case cad::FeatureType::Chamfer: m_commands->start(std::make_unique<ChamferCommand>(ctx, id)); return;
    case cad::FeatureType::Hole: m_commands->start(std::make_unique<HoleCommand>(ctx, id)); return;
    case cad::FeatureType::Combine: m_commands->start(std::make_unique<CombineCommand>(ctx, id)); return;
    case cad::FeatureType::ConstructionPlane: m_commands->start(std::make_unique<PlaneCommand>(ctx, id)); return;
    }
}

void MainWindow::showMarkingMenu(QPoint canvasPos) {
    auto a = [this](const char *name) { return action(QString::fromLatin1(name)); };
    if(m_sketch->active()) {
        m_marking->setRing({a("finishSketch"), a("sketchLine"), a("sketchRectangle"), a("sketchCircle"),
                            a("sketchDelete"), a("redo"), a("undo"), a("sketchDimension")});
        m_marking->setList({a("sketchLookAt"), a("sketchConstruction"), a("sketchArc"), a("sketchPoint"),
                            a("sketchCenterRectangle")});
    } else {
        QAction *repeat = a("repeatCommand");
        QAction *last = action(m_lastCommand);
        repeat->setEnabled(last != nullptr);
        repeat->setText(last ? tr("Repeat %1").arg(last->text()) : tr("Repeat"));
        m_marking->setRing({repeat, a("createSketch"), a("extrude"), a("fillet"), nullptr, a("redo"), a("undo"), a("hole")});
        m_marking->setList({a("fitView"), a("homeView"), a("toggleOrigin")});
    }
    m_marking->open(canvasPos);
}

McpDialog *MainWindow::openMcpDialog() {
    if(!m_mcpDialog) {
        m_mcpDialog = new McpDialog(*m_mcp, *m_mcpLog, this);
        m_mcpDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    // Connected: the log is what is interesting; otherwise the settings.
    if(m_mcp->state() == McpServer::State::Connected) m_mcpDialog->showLog();
    m_mcpDialog->show();
    m_mcpDialog->raise();
    m_mcpDialog->activateWindow();
    return m_mcpDialog;
}

SettingsDialog *MainWindow::openSettings() {
    auto *dlg = new SettingsDialog(m_viewport->mouseBindings(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        const MouseBindings b = dlg->bindings();
        b.save();
        m_viewport->setMouseBindings(b);
        ExtrudeCommand::setAutoOperation(dlg->autoOperation());
    });
    dlg->open();
    return dlg;
}

ExportDialog *MainWindow::openExportDialog(ExportJob::Format format) {
    // Export the design, not a command's preview or a result still computing.
    finishInteractions();
    m_modelView->showDocumentNow();
    m_recompute->supersedeRequests();
    auto *dlg = new ExportDialog(*m_modelView, *m_document, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFormat(format);
    dlg->open();
    return dlg;
}

// --- actions, ribbon, menus -------------------------------------------------------------

void MainWindow::buildActions() {
    makeAction("newDocument", tr("New Design"), IconId::New, QKeySequence::New, [this] { newDocument(); });
    makeAction("open", tr("Open..."), IconId::Open, QKeySequence::Open, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Open"), {}, tr("Cadly designs (*.cadly)"));
        if(!p.isEmpty()) openFile(p);
    });
    makeAction("save", tr("Save"), IconId::Save, QKeySequence::Save, [this] {
        QString p = m_path;
        if(p.isEmpty())
            p = QFileDialog::getSaveFileName(this, tr("Save"), QStringLiteral("design.cadly"), tr("Cadly designs (*.cadly)"));
        if(!p.isEmpty()) saveFile(p);
    });
    makeAction("saveAs", tr("Save As..."), IconId::Save, QKeySequence::SaveAs, [this] {
        const QString p =
            QFileDialog::getSaveFileName(this, tr("Save As"), m_path.isEmpty() ? QStringLiteral("design.cadly") : m_path,
                                         tr("Cadly designs (*.cadly)"));
        if(!p.isEmpty()) saveFile(p);
    });
    makeAction("export", tr("Export..."), IconId::ExportStep, {}, [this] { openExportDialog(ExportJob::Format::Step); });
    QAction *print = makeAction("print3d", tr("3D Print"), IconId::Print3D, QKeySequence(Qt::CTRL | Qt::Key_P),
                                [this] { openExportDialog(ExportJob::Format::Stl); });
    print->setToolTip(tr("3D Print (%1): export a watertight STL, checked for printing")
                          .arg(print->shortcut().toString(QKeySequence::NativeText)));
    QAction *settings = makeAction("settings", tr("Settings..."), IconId::Settings, QKeySequence::Preferences,
                                   [this] { openSettings(); });
    settings->setMenuRole(QAction::PreferencesRole);
    makeAction("mcpServer", tr("MCP Server..."), IconId::McpServer, {}, [this] { openMcpDialog(); });
    makeAction("undo", tr("Undo"), IconId::Undo, QKeySequence::Undo, [this] { undo(); });
    QAction *redo = makeAction("redo", tr("Redo"), IconId::Redo, QKeySequence::Redo, [this] { this->redo(); });
    redo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    makeAction("fitView", tr("Fit"), IconId::Fit, {}, [this] { m_viewport->fitAll(); });
    makeAction("homeView", tr("Home"), IconId::Home, {}, [this] { m_viewport->setStandardView(StandardView::Home); });
    makeAction("toggleOrigin", tr("Show / Hide Origin"), IconId::Origin, {}, [this] {
        m_modelView->setOriginVisible(!m_modelView->originVisible());
        m_browser->rebuild();
    });
    makeAction("repeatCommand", tr("Repeat"), IconId::Repeat, {}, [this] {
        if(QAction *last = action(m_lastCommand)) last->trigger();
    });

    // SOLID workspace.
    makeAction("createSketch", tr("Create Sketch"), IconId::Sketch, {}, [this] {
        if(m_commands->active()) m_commands->cancel();
        m_lastCommand = QStringLiteral("createSketch");
        m_sketch->startCreateSketch();
    });
    makeAction("extrude", tr("Extrude"), IconId::Extrude, QKeySequence(Qt::Key_E), [this] { startExtrude(); });
    const CommandContext ctx{m_document.get(), m_modelView, m_viewport, m_commandPanel};
    makeAction("hole", tr("Hole"), IconId::Hole, QKeySequence(Qt::Key_H),
               [this, ctx] { startCommand(QStringLiteral("hole"), std::make_unique<HoleCommand>(ctx)); });
    makeAction("fillet", tr("Fillet"), IconId::Fillet, QKeySequence(Qt::Key_F),
               [this, ctx] { startCommand(QStringLiteral("fillet"), std::make_unique<FilletCommand>(ctx)); });
    makeAction("chamfer", tr("Chamfer"), IconId::Chamfer, {},
               [this, ctx] { startCommand(QStringLiteral("chamfer"), std::make_unique<ChamferCommand>(ctx)); });
    makeAction("combine", tr("Combine"), IconId::Combine, {},
               [this, ctx] { startCommand(QStringLiteral("combine"), std::make_unique<CombineCommand>(ctx)); });
    makeAction("offsetPlane", tr("Offset Plane"), IconId::Plane, {},
               [this, ctx] { startCommand(QStringLiteral("offsetPlane"), std::make_unique<PlaneCommand>(ctx)); });
    makeAction("sectionAnalysis", tr("Section Analysis"), IconId::Section, {}, [this, ctx] {
        startCommand(QStringLiteral("sectionAnalysis"), std::make_unique<SectionCommand>(ctx));
    });

    // SKETCH workspace.
    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    const std::tuple<const char *, SketchToolKind, IconId, QKeySequence> tools[] = {
        {"sketchLine", SketchToolKind::Line, IconId::Line, QKeySequence(Qt::Key_L)},
        {"sketchRectangle", SketchToolKind::Rectangle, IconId::Rectangle, QKeySequence(Qt::Key_R)},
        {"sketchCenterRectangle", SketchToolKind::CenterRectangle, IconId::CenterRectangle, {}},
        {"sketchCircle", SketchToolKind::Circle, IconId::Circle, QKeySequence(Qt::Key_C)},
        {"sketchArc", SketchToolKind::Arc, IconId::Arc, {}},
        {"sketchPoint", SketchToolKind::Point, IconId::Point, {}},
        {"sketchDimension", SketchToolKind::Dimension, IconId::Dimension, QKeySequence(Qt::Key_D)},
        {"constraintCoincident", SketchToolKind::Coincident, IconId::Coincident, {}},
        {"constraintHorizontalVertical", SketchToolKind::HorizontalVertical, IconId::HorizontalVertical, {}},
        {"constraintParallel", SketchToolKind::Parallel, IconId::Parallel, {}},
        {"constraintPerpendicular", SketchToolKind::Perpendicular, IconId::Perpendicular, {}},
        {"constraintTangent", SketchToolKind::Tangent, IconId::Tangent, {}},
        {"constraintEqual", SketchToolKind::Equal, IconId::Equal, {}},
        {"constraintMidpoint", SketchToolKind::Midpoint, IconId::Midpoint, {}},
        {"constraintConcentric", SketchToolKind::Concentric, IconId::Concentric, {}},
        {"constraintFix", SketchToolKind::Fix, IconId::Fix, {}},
        {"constraintSymmetric", SketchToolKind::Symmetric, IconId::Symmetric, {}},
    };
    for(const auto &[name, kind, id, key] : tools) {
        const SketchToolKind k = kind;
        QAction *a = makeAction(name, sketchToolName(k), id, key, [this, k] { m_sketch->setTool(k); });
        a->setCheckable(true);
        m_toolGroup->addAction(a);
        m_toolActions[k] = a;
        m_sketchOnly.push_back(a);
    }
    m_sketchOnly.push_back(makeAction("sketchConstruction", tr("Normal / Construction"), IconId::Construction,
                                      QKeySequence(Qt::Key_X), [this] { m_sketch->toggleConstruction(); }));
    QAction *del = makeAction("sketchDelete", tr("Delete"), IconId::Delete, QKeySequence::Delete,
                              [this] { m_sketch->deleteSelection(); });
    del->setShortcuts({QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
    m_sketchOnly.push_back(del);
    m_sketchOnly.push_back(makeAction("sketchLookAt", tr("Look At"), IconId::LookAt, {}, [this] { m_sketch->lookAt(); }));
    m_sketchOnly.push_back(
        makeAction("finishSketch", tr("Finish Sketch"), IconId::FinishSketch, {}, [this] { m_sketch->finish(); }));
}

void MainWindow::buildRibbon() {
    // File / undo buttons left of the workspace tabs, as in Fusion's toolbar.
    auto *fileButton = new MenuButton(tr("File"), MenuButton::Style::Button, m_ribbon);
    fileButton->setObjectName(QStringLiteral("fileMenuButton"));
    fileButton->setIcon(icon(IconId::Open));
    fileButton->setIconSize(QSize(18, 18));
    QMenu *fileMenu = fileButton->menu();
    for(const char *name : {"newDocument", "open", "save", "saveAs"}) fileMenu->addAction(action(QString::fromLatin1(name)));
    fileMenu->addSeparator();
    fileMenu->addAction(action(QStringLiteral("export")));
    fileMenu->addAction(action(QStringLiteral("print3d")));
    m_ribbon->addLeadingWidget(fileButton);
    for(const char *name : {"save", "undo", "redo"}) {
        auto *b = new QToolButton(m_ribbon);
        b->setDefaultAction(action(QString::fromLatin1(name)));
        b->setAutoRaise(true);
        b->setIconSize(QSize(18, 18));
        b->setFocusPolicy(Qt::NoFocus);
        m_ribbon->addLeadingWidget(b);
    }

    m_mcpButton = new McpButton(m_ribbon);
    connect(m_mcpButton, &QAbstractButton::clicked, this, [this] { openMcpDialog(); });
    m_ribbon->addTrailingWidget(m_mcpButton);

    m_solidTab = m_ribbon->addTab(tr("SOLID"));
    RibbonGroup *create = m_solidTab->addGroup(tr("CREATE"));
    create->addAction(action(QStringLiteral("createSketch")));
    create->addAction(action(QStringLiteral("extrude")));
    create->addAction(action(QStringLiteral("hole")));
    RibbonGroup *modify = m_solidTab->addGroup(tr("MODIFY"));
    modify->addAction(action(QStringLiteral("fillet")));
    modify->addAction(action(QStringLiteral("chamfer")));
    modify->addAction(action(QStringLiteral("combine")));
    RibbonGroup *construct = m_solidTab->addGroup(tr("CONSTRUCT"));
    construct->addAction(action(QStringLiteral("offsetPlane")));
    RibbonGroup *inspect = m_solidTab->addGroup(tr("INSPECT"));
    inspect->addAction(action(QStringLiteral("sectionAnalysis")));
    RibbonGroup *make = m_solidTab->addGroup(tr("MAKE"));
    make->addAction(action(QStringLiteral("print3d")));
    m_solidTab->addStretch();

    m_sketchTab = m_ribbon->addTab(tr("SKETCH"));
    RibbonGroup *draw = m_sketchTab->addGroup(tr("CREATE"));
    for(const char *name : {"sketchLine", "sketchRectangle", "sketchCircle", "sketchArc", "sketchDimension"})
        draw->addAction(action(QString::fromLatin1(name)));
    draw->addAction(action(QStringLiteral("sketchCenterRectangle")), false);
    draw->addAction(action(QStringLiteral("sketchPoint")), false);
    draw->addSeparator();
    draw->addAction(action(QStringLiteral("sketchConstruction")), false);
    RibbonGroup *constraints = m_sketchTab->addGroup(tr("CONSTRAINTS"));
    for(const char *name : {"constraintCoincident", "constraintHorizontalVertical", "constraintParallel",
                            "constraintPerpendicular", "constraintTangent", "constraintEqual", "constraintMidpoint",
                            "constraintConcentric", "constraintFix", "constraintSymmetric"})
        constraints->addAction(action(QString::fromLatin1(name)));
    m_sketchTab->addStretch();
    RibbonGroup *finish = m_sketchTab->addGroup(tr("FINISH SKETCH"));
    finish->addAction(action(QStringLiteral("finishSketch")));
}

void MainWindow::buildMenus() {
    QMenu *file = menuBar()->addMenu(tr("&File"));
    for(const char *name : {"newDocument", "open", "save", "saveAs"}) file->addAction(action(QString::fromLatin1(name)));
    file->addSeparator();
    file->addAction(action(QStringLiteral("export")));
    file->addAction(action(QStringLiteral("print3d")));
    file->addSeparator();
    file->addAction(action(QStringLiteral("mcpServer")));
    file->addAction(action(QStringLiteral("settings")));

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(action(QStringLiteral("undo")));
    edit->addAction(action(QStringLiteral("redo")));

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *fit = view->addAction(tr("Fit"), this, [this] { m_viewport->fitAll(); });
    fit->setShortcut(Qt::Key_F6);
    view->addAction(tr("Home"), this, [this] { m_viewport->setStandardView(StandardView::Home); });
    view->addSeparator();
    const std::pair<const char *, StandardView> views[] = {
        {"Front", StandardView::Front}, {"Back", StandardView::Back}, {"Left", StandardView::Left},
        {"Right", StandardView::Right}, {"Top", StandardView::Top},   {"Bottom", StandardView::Bottom}};
    for(const auto &[name, v] : views) {
        const StandardView sv = v;
        view->addAction(tr(name), this, [this, sv] { m_viewport->setStandardView(sv); });
    }
    view->addSeparator();
    // Visual styles (also in the navigation bar's display menu; both follow the canvas).
    auto *styles = new QActionGroup(this);
    const std::tuple<const char *, const char *, DisplayStyle> styleList[] = {
        {"displayShadedEdges", QT_TR_NOOP("Shaded with Visible Edges"), DisplayStyle::ShadedWithEdges},
        {"displayShaded", QT_TR_NOOP("Shaded"), DisplayStyle::Shaded},
        {"displayWireframe", QT_TR_NOOP("Wireframe"), DisplayStyle::Wireframe},
        {"displayRendered", QT_TR_NOOP("Rendered"), DisplayStyle::Rendered}};
    for(const auto &[name, text, s] : styleList) {
        const DisplayStyle ds = s;
        QAction *a = view->addAction(tr(text), this, [this, ds] { m_viewport->setDisplayStyle(ds); });
        a->setObjectName(QString::fromLatin1(name));
        a->setCheckable(true);
        a->setChecked(ds == m_viewport->displayStyle());
        a->setData(int(ds));
        styles->addAction(a);
        m_actions[a->objectName()] = a;
    }
    connect(m_viewport, &Viewport::displayStyleChanged, this, [styles](DisplayStyle ds) {
        for(QAction *a : styles->actions()) a->setChecked(a->data().toInt() == int(ds));
    });
    view->addSeparator();
    view->addAction(action(QStringLiteral("toggleOrigin")));
}

void MainWindow::onSketchActive(bool active) {
    for(QAction *a : m_sketchOnly) a->setEnabled(active);
    m_ribbon->setTabVisible(m_sketchTab, active);
    m_ribbon->setCurrentTab(active ? m_sketchTab : m_solidTab);
    if(!active) {
        onSketchTool(SketchToolKind::Select);
        statusBar()->clearMessage(); // the last tool's prompt
    }
    updateStats();
    updateActions();
}

void MainWindow::onSketchTool(SketchToolKind kind) {
    for(const auto &[k, a] : m_toolActions) {
        const QSignalBlocker block(a);
        a->setChecked(k == kind);
    }
}

} // namespace cadly
