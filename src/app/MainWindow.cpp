#include "MainWindow.h"

#include "viewport/Viewport.h"

#include <QLabel>
#include <QStatusBar>

namespace cadly {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Cadly"));
    resize(1400, 900);

    m_viewport = new Viewport(this);
    setCentralWidget(m_viewport);

    // Bottom-right selection statistics, as in Fusion 360.
    m_selectionStats = new QLabel(this);
    m_selectionStats->setObjectName(QStringLiteral("selectionStats"));
    statusBar()->addPermanentWidget(m_selectionStats);
}

MainWindow::~MainWindow() = default;

} // namespace cadly
