#include "viewport/NavBar.h"

#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include <QActionGroup>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QToolButton>

namespace cadly {

namespace {

QToolButton *makeButton(QWidget *parent, IconId id, const QString &tip, bool checkable = false) {
    auto *b = new QToolButton(parent);
    b->setIcon(icon(id));
    b->setIconSize(QSize(20, 20));
    b->setToolTip(tip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

} // namespace

NavBar::NavBar(Viewport *viewport) : QFrame(viewport), m_viewport(viewport) {
    setObjectName(QStringLiteral("navBar"));
    setStyleSheet(QStringLiteral(
        "#navBar { background: rgba(250, 251, 253, 225); border: 1px solid rgba(120, 130, 145, 110);"
        " border-radius: 6px; }"
        "QToolButton { padding: 2px; border-radius: 4px; }"
        "QToolButton:hover { background: rgba(60, 130, 220, 40); }"
        "QToolButton:checked { background: rgba(60, 130, 220, 70); }"));
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 3, 6, 3);
    lay->setSpacing(2);

    m_orbit = makeButton(this, IconId::Orbit, tr("Orbit (right drag by default; see Settings)"), true);
    m_pan = makeButton(this, IconId::Pan, tr("Pan (middle drag by default; see Settings)"), true);
    m_zoom = makeButton(this, IconId::Zoom, tr("Zoom (scroll wheel)"), true);
    m_fit = makeButton(this, IconId::Fit, tr("Fit (F6)"));
    m_display = makeButton(this, IconId::Display, tr("Display settings"));
    m_grid = makeButton(this, IconId::Grid, tr("Layout grid"), true);
    m_camera = makeButton(this, IconId::Camera, tr("Camera"));
    for(QToolButton *b : {m_orbit, m_pan, m_zoom, m_fit, m_display, m_grid, m_camera}) lay->addWidget(b);
    m_orbit->setObjectName(QStringLiteral("navOrbit"));
    m_pan->setObjectName(QStringLiteral("navPan"));
    m_zoom->setObjectName(QStringLiteral("navZoom"));
    m_fit->setObjectName(QStringLiteral("navFit"));
    m_grid->setObjectName(QStringLiteral("navGrid"));

    auto modeButton = [this](QToolButton *b, Viewport::NavMode mode) {
        connect(b, &QToolButton::clicked, this, [this, b, mode](bool on) {
            m_viewport->setNavMode(on ? mode : Viewport::NavMode::Select);
            syncFromViewport();
            Q_UNUSED(b);
        });
    };
    modeButton(m_orbit, Viewport::NavMode::Orbit);
    modeButton(m_pan, Viewport::NavMode::Pan);
    modeButton(m_zoom, Viewport::NavMode::Zoom);
    connect(m_fit, &QToolButton::clicked, this, [this] { m_viewport->fitAll(); });
    connect(m_grid, &QToolButton::toggled, this, [this](bool on) { m_viewport->setGridVisible(on); });

    auto *displayMenu = new QMenu(this);
    auto *styles = new QActionGroup(displayMenu);
    auto addStyle = [&](const QString &text, DisplayStyle s) {
        QAction *a = displayMenu->addAction(text);
        a->setCheckable(true);
        a->setData(int(s));
        styles->addAction(a);
        connect(a, &QAction::triggered, this, [this, s] { m_viewport->setDisplayStyle(s); });
    };
    displayMenu->addSection(tr("Visual Style"));
    addStyle(tr("Shaded with Visible Edges"), DisplayStyle::ShadedWithEdges);
    addStyle(tr("Shaded"), DisplayStyle::Shaded);
    addStyle(tr("Wireframe"), DisplayStyle::Wireframe);
    addStyle(tr("Rendered"), DisplayStyle::Rendered);
    connect(displayMenu, &QMenu::aboutToShow, this, [this, styles] {
        for(QAction *a : styles->actions()) a->setChecked(a->data().toInt() == int(m_viewport->displayStyle()));
    });
    m_display->setMenu(displayMenu);
    m_display->setPopupMode(QToolButton::InstantPopup);

    auto *cameraMenu = new QMenu(this);
    QAction *persp = cameraMenu->addAction(tr("Perspective"));
    QAction *ortho = cameraMenu->addAction(tr("Orthographic"));
    auto *proj = new QActionGroup(cameraMenu);
    for(QAction *a : {persp, ortho}) {
        a->setCheckable(true);
        proj->addAction(a);
    }
    connect(persp, &QAction::triggered, this, [this] { m_viewport->setOrthographic(false); });
    connect(ortho, &QAction::triggered, this, [this] { m_viewport->setOrthographic(true); });
    connect(cameraMenu, &QMenu::aboutToShow, this, [this, persp, ortho] {
        persp->setChecked(!m_viewport->camera().orthographic);
        ortho->setChecked(m_viewport->camera().orthographic);
    });
    m_camera->setMenu(cameraMenu);
    m_camera->setPopupMode(QToolButton::InstantPopup);

    syncFromViewport();
    adjustSize();
}

void NavBar::syncFromViewport() {
    const auto mode = m_viewport->navMode();
    m_orbit->setChecked(mode == Viewport::NavMode::Orbit);
    m_pan->setChecked(mode == Viewport::NavMode::Pan);
    m_zoom->setChecked(mode == Viewport::NavMode::Zoom);
    const QSignalBlocker block(m_grid);
    m_grid->setChecked(m_viewport->gridVisible());
}

void NavBar::reposition() {
    adjustSize();
    move((m_viewport->width() - width()) / 2, m_viewport->height() - height() - 10);
    raise();
}

ViewportOverlay::ViewportOverlay(Viewport *viewport) : QWidget(viewport), m_viewport(viewport) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
}

void ViewportOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    m_viewport->paintOverlay(p);
}

} // namespace cadly
