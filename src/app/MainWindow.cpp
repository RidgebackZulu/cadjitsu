#include "MainWindow.h"

#include "model/ModelView.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "ui/Icons.h"
#include "ui/Ribbon.h"
#include "viewport/Viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
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
    m_viewport = new Viewport(central);
    layout->addWidget(m_ribbon);
    layout->addWidget(m_viewport, 1);
    setCentralWidget(central);

    m_modelView = new ModelView(*m_document, m_viewport, this);
    m_sketch = new SketchMode(*m_document, m_viewport, m_modelView, this);

    // Bottom-right selection statistics, as in Fusion 360.
    m_selectionStats = new QLabel(this);
    m_selectionStats->setObjectName(QStringLiteral("selectionStats"));
    m_selectionStats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(m_selectionStats);
    connect(m_modelView, &ModelView::statsChanged, this, &MainWindow::updateStats);
    connect(m_sketch, &SketchMode::statsChanged, this, &MainWindow::updateStats);
    connect(m_sketch, &SketchMode::message, this, [this](const QString &text) { statusBar()->showMessage(text, 8000); });
    connect(m_sketch, &SketchMode::activeChanged, this, &MainWindow::onSketchActive);
    connect(m_sketch, &SketchMode::toolChanged, this, &MainWindow::onSketchTool);
    connect(m_sketch, &SketchMode::finished, this, [this] { updateTitle(); });
    connect(m_viewport, &Viewport::contextMenuRequested, this, [this](const QPoint &globalPos, const PickHit &) {
        if(!m_sketch->active() && !m_sketch->pickingPlane()) showCanvasMenu(globalPos);
    });

    buildActions();
    buildRibbon();
    buildMenus();
    onSketchActive(false);
    updateTitle();
    refresh();
}

MainWindow::~MainWindow() {
    // The sketch workspace uses the viewport; take it down while the viewport exists.
    delete m_sketch;
    m_sketch = nullptr;
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

void MainWindow::refresh() {
    m_modelView->refresh();
    updateStats();
}

void MainWindow::updateTitle() {
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName();
    setWindowTitle(name + QStringLiteral(" — Cadly"));
}

void MainWindow::updateStats() {
    if(m_sketch->active()) m_selectionStats->setText(m_sketch->editor()->selectionStats());
    else m_selectionStats->setText(m_modelView->statsText());
}

void MainWindow::newDocument() {
    if(m_sketch->active()) m_sketch->finish();
    m_sketch->cancelCreateSketch();
    m_document->clear();
    m_path.clear();
    updateTitle();
    refresh();
    m_viewport->setStandardView(StandardView::Home, false);
}

bool MainWindow::openFile(const QString &path) {
    if(m_sketch->active()) m_sketch->finish();
    m_sketch->cancelCreateSketch();
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
    if(m_sketch->active()) m_sketch->finish();
    std::string error;
    if(!m_document->save(path.toStdString(), error)) {
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n%2").arg(path, QString::fromStdString(error)));
        return false;
    }
    m_path = path;
    updateTitle();
    return true;
}

void MainWindow::undo() {
    if(m_sketch->active()) {
        if(!m_sketch->undo()) statusBar()->showMessage(tr("Nothing to undo in this sketch."), 4000);
        return;
    }
    m_sketch->cancelCreateSketch();
    if(m_document->undo()) {
        statusBar()->showMessage(tr("Undo %1").arg(QString::fromStdString(m_document->redoLabel())), 4000);
        refresh();
    }
}

void MainWindow::redo() {
    if(m_sketch->active()) {
        m_sketch->redo();
        return;
    }
    if(m_document->redo()) {
        statusBar()->showMessage(tr("Redo %1").arg(QString::fromStdString(m_document->undoLabel())), 4000);
        refresh();
    }
}

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
    makeAction("undo", tr("Undo"), IconId::Undo, QKeySequence::Undo, [this] { undo(); });
    QAction *redo = makeAction("redo", tr("Redo"), IconId::Redo, QKeySequence::Redo, [this] { this->redo(); });
    redo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});

    // SOLID workspace.
    makeAction("createSketch", tr("Create Sketch"), IconId::Sketch, {}, [this] { m_sketch->startCreateSketch(); });
    const std::tuple<const char *, QString, IconId, QString> later[] = {
        {"extrude", tr("Extrude"), IconId::Extrude, QStringLiteral("E")},
        {"hole", tr("Hole"), IconId::Hole, QStringLiteral("H")},
        {"fillet", tr("Fillet"), IconId::Fillet, QStringLiteral("F")},
        {"chamfer", tr("Chamfer"), IconId::Chamfer, {}},
        {"combine", tr("Combine"), IconId::Combine, {}},
        {"offsetPlane", tr("Offset Plane"), IconId::Plane, {}},
        {"sectionAnalysis", tr("Section Analysis"), IconId::Section, {}},
    };
    for(const auto &[name, text, id, key] : later) {
        QAction *a = makeAction(name, text, id, {}, {});
        a->setEnabled(false);
        a->setToolTip(tr("%1 (coming in a later milestone)").arg(text));
        Q_UNUSED(key);
    }

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
    QAction *del = makeAction("sketchDelete", tr("Delete"), IconId::Error, QKeySequence::Delete,
                              [this] { m_sketch->deleteSelection(); });
    del->setShortcuts({QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
    del->setIcon(QIcon());
    m_sketchOnly.push_back(del);
    m_sketchOnly.push_back(makeAction("sketchLookAt", tr("Look At"), IconId::LookAt, {}, [this] { m_sketch->lookAt(); }));
    m_sketchOnly.push_back(
        makeAction("finishSketch", tr("Finish Sketch"), IconId::FinishSketch, {}, [this] { m_sketch->finish(); }));
}

void MainWindow::buildRibbon() {
    // File / undo buttons left of the workspace tabs, as in Fusion's toolbar.
    auto *fileButton = new QToolButton(m_ribbon);
    fileButton->setObjectName(QStringLiteral("fileMenuButton"));
    fileButton->setText(tr("File"));
    fileButton->setIcon(icon(IconId::Open));
    fileButton->setPopupMode(QToolButton::InstantPopup);
    fileButton->setAutoRaise(true);
    fileButton->setFocusPolicy(Qt::NoFocus);
    auto *fileMenu = new QMenu(fileButton);
    fileMenu->addAction(action(QStringLiteral("newDocument")));
    fileMenu->addAction(action(QStringLiteral("open")));
    fileMenu->addAction(action(QStringLiteral("save")));
    fileButton->setMenu(fileMenu);
    m_ribbon->addLeadingWidget(fileButton);
    for(const char *name : {"save", "undo", "redo"}) {
        auto *b = new QToolButton(m_ribbon);
        b->setDefaultAction(action(QString::fromLatin1(name)));
        b->setAutoRaise(true);
        b->setIconSize(QSize(18, 18));
        b->setFocusPolicy(Qt::NoFocus);
        m_ribbon->addLeadingWidget(b);
    }

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
    file->addAction(action(QStringLiteral("newDocument")));
    file->addAction(action(QStringLiteral("open")));
    file->addAction(action(QStringLiteral("save")));

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
    auto *styles = new QActionGroup(this);
    const std::pair<const char *, DisplayStyle> styleList[] = {{"Shaded with Visible Edges", DisplayStyle::ShadedWithEdges},
                                                               {"Shaded", DisplayStyle::Shaded},
                                                               {"Wireframe", DisplayStyle::Wireframe}};
    for(const auto &[name, s] : styleList) {
        const DisplayStyle ds = s;
        QAction *a = view->addAction(tr(name), this, [this, ds] { m_viewport->setDisplayStyle(ds); });
        a->setCheckable(true);
        a->setChecked(ds == DisplayStyle::ShadedWithEdges);
        styles->addAction(a);
    }
}

// Right-click on the canvas outside sketch mode.
void MainWindow::showCanvasMenu(const QPoint &globalPos) {
    QMenu menu(this);
    menu.addAction(action(QStringLiteral("createSketch")));
    QMenu *edit = menu.addMenu(icon(IconId::SketchNode), tr("Edit Sketch"));
    for(const auto &f : m_document->features()) {
        if(f->type() != cad::FeatureType::Sketch) continue;
        const cad::FeatureId id = f->id;
        edit->addAction(QString::fromStdString(f->name), this, [this, id] { m_sketch->editSketch(id); });
    }
    edit->setEnabled(!edit->isEmpty());
    menu.addSeparator();
    menu.addAction(action(QStringLiteral("undo")));
    menu.addAction(action(QStringLiteral("redo")));
    menu.addSeparator();
    menu.addAction(icon(IconId::Fit), tr("Fit"), this, [this] { m_viewport->fitAll(); });
    menu.exec(globalPos);
}

void MainWindow::onSketchActive(bool active) {
    for(QAction *a : m_sketchOnly) a->setEnabled(active);
    action(QStringLiteral("createSketch"))->setEnabled(!active);
    m_ribbon->setTabVisible(m_sketchTab, active);
    m_ribbon->setCurrentTab(active ? m_sketchTab : m_solidTab);
    if(!active) onSketchTool(SketchToolKind::Select);
    updateStats();
}

void MainWindow::onSketchTool(SketchToolKind kind) {
    for(const auto &[k, a] : m_toolActions) {
        const QSignalBlocker block(a);
        a->setChecked(k == kind);
    }
}

} // namespace cadly
