#include "viewport/NavBar.h"

#include "ui/Icons.h"
#include "viewport/Viewport.h"

#include <QActionGroup>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>

namespace cadly {

namespace {

// A navigation bar button: the icon on a rounded hover / checked plate, and
// for buttons with a menu a small chevron of its own to the right of the icon
// (Qt's menu indicator sits on top of the icon).
class NavButton : public QToolButton {
public:
    NavButton(QWidget *parent, IconId id, const QString &tip, bool checkable) : QToolButton(parent) {
        setIcon(cadly::icon(id));
        setIconSize(QSize(20, 20));
        setToolTip(tip);
        setCheckable(checkable);
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
    }
    QSize sizeHint() const override { return {menu() ? 40 : 30, 28}; }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void enterEvent(QEnterEvent *e) override {
        QToolButton::enterEvent(e);
        update();
    }
    void leaveEvent(QEvent *e) override {
        QToolButton::leaveEvent(e);
        update();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        const bool open = isDown() || (menu() && menu()->isVisible());
        if(isChecked() || open) {
            p.setPen(QPen(QColor(47, 123, 224, 150), 1.0));
            p.setBrush(QColor(47, 123, 224, 46));
            p.drawRoundedRect(r, 6, 6);
        } else if(underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(47, 123, 224, 26));
            p.drawRoundedRect(r, 6, 6);
        }
        const QSize is = iconSize();
        const double iconLeft = menu() ? r.left() + 5 : r.center().x() - is.width() / 2.0;
        const QRect ir(int(std::round(iconLeft)), int(std::round(r.center().y() - is.height() / 2.0)), is.width(),
                       is.height());
        icon().paint(&p, ir, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
        if(menu()) {
            const double cx = r.right() - 6.5, cy = r.center().y() + 0.5;
            QPainterPath v;
            v.moveTo(cx - 3.2, cy - 1.8);
            v.lineTo(cx, cy + 1.6);
            v.lineTo(cx + 3.2, cy - 1.8);
            p.setPen(QPen(underMouse() || open ? QColor(28, 88, 180) : QColor(92, 104, 122), 1.5, Qt::SolidLine,
                          Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(v);
        }
    }
};

QToolButton *makeButton(QWidget *parent, IconId id, const QString &tip, bool checkable = false) {
    return new NavButton(parent, id, tip, checkable);
}

// A thin vertical divider between groups of buttons.
QFrame *divider(QWidget *parent) {
    auto *f = new QFrame(parent);
    f->setObjectName(QStringLiteral("navDivider"));
    f->setFixedSize(1, 18);
    return f;
}

} // namespace

NavBar::NavBar(Viewport *viewport) : QFrame(viewport), m_viewport(viewport) {
    setObjectName(QStringLiteral("navBar"));
    setStyleSheet(QStringLiteral(
        "#navBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(255, 255, 255, 246),"
        " stop:1 rgba(241, 244, 249, 246)); border: 1px solid rgba(118, 132, 152, 120); border-radius: 9px; }"
        "#navDivider { background: rgba(118, 132, 152, 90); border: none; }"
        "QToolButton { border: none; background: transparent; }"
        "QToolButton::menu-indicator { image: none; width: 0; }"));
    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(14);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(20, 35, 60, 55));
    setGraphicsEffect(shadow);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(6, 3, 6, 3);
    lay->setSpacing(1);

    m_orbit = makeButton(this, IconId::Orbit, tr("Orbit (right drag by default; see Settings)"), true);
    m_pan = makeButton(this, IconId::Pan, tr("Pan (middle drag by default; see Settings)"), true);
    m_zoom = makeButton(this, IconId::Zoom, tr("Zoom (scroll wheel)"), true);
    m_fit = makeButton(this, IconId::Fit, tr("Fit (F6)"));
    m_display = makeButton(this, IconId::Display, tr("Display settings"));
    m_grid = makeButton(this, IconId::Grid, tr("Layout grid"), true);
    m_camera = makeButton(this, IconId::Camera, tr("Camera"));
    for(QToolButton *b : {m_orbit, m_pan, m_zoom, m_fit}) lay->addWidget(b);
    lay->addSpacing(4);
    lay->addWidget(divider(this), 0, Qt::AlignVCenter);
    lay->addSpacing(4);
    for(QToolButton *b : {m_display, m_grid, m_camera}) lay->addWidget(b);
    m_orbit->setObjectName(QStringLiteral("navOrbit"));
    m_pan->setObjectName(QStringLiteral("navPan"));
    m_zoom->setObjectName(QStringLiteral("navZoom"));
    m_fit->setObjectName(QStringLiteral("navFit"));
    m_grid->setObjectName(QStringLiteral("navGrid"));
    m_display->setObjectName(QStringLiteral("navDisplay"));
    m_camera->setObjectName(QStringLiteral("navCamera"));

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
    move((m_viewport->width() - width()) / 2, m_viewport->height() - height() - 12);
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
