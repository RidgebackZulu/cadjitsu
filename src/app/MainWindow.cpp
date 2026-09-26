#include "MainWindow.h"

#include "model/ModelView.h"
#include "viewport/Viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

namespace cadly {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_document(std::make_unique<cad::Document>()) {
    resize(1400, 900);

    m_viewport = new Viewport(this);
    setCentralWidget(m_viewport);
    m_modelView = new ModelView(*m_document, m_viewport, this);

    // Bottom-right selection statistics, as in Fusion 360.
    m_selectionStats = new QLabel(this);
    m_selectionStats->setObjectName(QStringLiteral("selectionStats"));
    m_selectionStats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addPermanentWidget(m_selectionStats);
    connect(m_modelView, &ModelView::statsChanged, m_selectionStats, &QLabel::setText);

    buildMenus();
    updateTitle();
    refresh();
}

MainWindow::~MainWindow() = default;

void MainWindow::refresh() { m_modelView->refresh(); }

void MainWindow::updateTitle() {
    const QString name = m_path.isEmpty() ? tr("Untitled") : QFileInfo(m_path).completeBaseName();
    setWindowTitle(name + QStringLiteral(" — Cadly"));
}

bool MainWindow::openFile(const QString &path) {
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
    std::string error;
    if(!m_document->save(path.toStdString(), error)) {
        QMessageBox::warning(this, tr("Save"), tr("Could not save %1:\n%2").arg(path, QString::fromStdString(error)));
        return false;
    }
    m_path = path;
    updateTitle();
    return true;
}

void MainWindow::buildMenus() {
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *open = file->addAction(tr("&Open..."), this, [this] {
        const QString p = QFileDialog::getOpenFileName(this, tr("Open"), {}, tr("Cadly designs (*.cadly)"));
        if(!p.isEmpty()) openFile(p);
    });
    open->setShortcut(QKeySequence::Open);
    QAction *save = file->addAction(tr("&Save"), this, [this] {
        QString p = m_path;
        if(p.isEmpty()) p = QFileDialog::getSaveFileName(this, tr("Save"), QStringLiteral("design.cadly"), tr("Cadly designs (*.cadly)"));
        if(!p.isEmpty()) saveFile(p);
    });
    save->setShortcut(QKeySequence::Save);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    QAction *fit = view->addAction(tr("Fit"), this, [this] { m_viewport->fitAll(); });
    fit->setShortcut(Qt::Key_F6);
    view->addAction(tr("Home"), this, [this] { m_viewport->setStandardView(StandardView::Home); });
    view->addSeparator();
    const std::pair<const char *, StandardView> views[] = {
        {"Front", StandardView::Front}, {"Back", StandardView::Back},   {"Left", StandardView::Left},
        {"Right", StandardView::Right}, {"Top", StandardView::Top},     {"Bottom", StandardView::Bottom}};
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

} // namespace cadly
