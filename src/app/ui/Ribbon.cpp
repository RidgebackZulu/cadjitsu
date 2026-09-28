#include "ui/Ribbon.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace cadjitsu {

MenuButton::MenuButton(const QString &text, Style style, QWidget *parent) : QToolButton(parent), m_style(style) {
    setText(text);
    setPopupMode(QToolButton::InstantPopup);
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    auto *menu = new QMenu(this);
    menu->setStyleSheet(menuStyleSheet());
    // Rounded corners need a see-through window behind them.
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    connect(menu, &QMenu::aboutToShow, this, [this] {
        m_open = true;
        update();
    });
    connect(menu, &QMenu::aboutToHide, this, [this] {
        m_open = false;
        update();
    });
    setMenu(menu);
}

QString MenuButton::menuStyleSheet() {
    return QStringLiteral(
        "QMenu { background: #ffffff; border: 1px solid #c9ced6; border-radius: 8px; padding: 4px; color: #1c2128; }"
        "QMenu::item { padding: 5px 22px 5px 8px; border-radius: 5px; margin: 1px 0; }"
        "QMenu::item:selected { background: #e8f0fb; color: #0f1a2a; }"
        "QMenu::item:disabled { color: #a0a6b0; }"
        "QMenu::icon { padding-left: 6px; }"
        "QMenu::separator { height: 1px; background: #e3e6eb; margin: 4px 8px; }");
}

QFont MenuButton::labelFont() const {
    QFont f = font();
    if(m_style == Style::Caption) {
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
    } else {
        f.setPixelSize(12);
        f.setWeight(QFont::Medium);
    }
    return f;
}

QSize MenuButton::sizeHint() const {
    const QFontMetrics fm(labelFont());
    const int iconW = icon().isNull() ? 0 : iconSize().width() + 6;
    const int h = m_style == Style::Caption ? 18 : 26;
    return QSize(8 + iconW + fm.horizontalAdvance(text()) + 5 + 8 + 8, h);
}

void MenuButton::enterEvent(QEnterEvent *e) {
    m_hover = true;
    update();
    QToolButton::enterEvent(e);
}

void MenuButton::leaveEvent(QEvent *e) {
    m_hover = false;
    update();
    QToolButton::leaveEvent(e);
}

void MenuButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool lit = m_hover || m_open;
    const QColor accent(26, 102, 201);
    const QColor ink = lit ? accent : (m_style == Style::Caption ? QColor(74, 82, 96) : QColor(28, 33, 40));
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if(lit) {
        p.setPen(m_open ? QPen(QColor(26, 102, 201, 70), 1) : Qt::NoPen);
        p.setBrush(QColor(26, 102, 201, m_open ? 30 : 20));
        p.drawRoundedRect(r, r.height() / 2 > 8 ? 6 : 5, r.height() / 2 > 8 ? 6 : 5);
    }
    int x = 8;
    if(!icon().isNull()) {
        const QSize is = iconSize();
        icon().paint(&p, QRect(x, (height() - is.height()) / 2, is.width(), is.height()));
        x += is.width() + 6;
    }
    const QFont f = labelFont();
    p.setFont(f);
    p.setPen(ink);
    const QFontMetrics fm(f);
    const int tw = fm.horizontalAdvance(text());
    p.drawText(QRect(x, 0, tw + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    // The chevron: a small open V (or ^ while the menu is open), stroked, not a glyph.
    const double cx = x + tw + 5 + 3.5, cy = height() / 2.0 + 0.5;
    const double w = 3.5, h = m_open ? -2.0 : 2.0;
    QPainterPath chevron;
    chevron.moveTo(cx - w, cy - h);
    chevron.lineTo(cx, cy + h);
    chevron.lineTo(cx + w, cy - h);
    p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(chevron);
}

RibbonGroup::RibbonGroup(const QString &title, QWidget *parent) : QWidget(parent) {
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 2, 4, 0);
    v->setSpacing(0);
    m_buttons = new QHBoxLayout;
    m_buttons->setSpacing(1);
    v->addLayout(m_buttons);
    m_caption = new MenuButton(title, MenuButton::Style::Caption, this);
    m_caption->setObjectName(QStringLiteral("ribbonGroupCaption"));
    v->addWidget(m_caption, 0, Qt::AlignHCenter);
}

void RibbonGroup::addAction(QAction *action, bool onBar) {
    m_caption->menu()->addAction(action);
    if(!onBar) return;
    auto *b = new QToolButton(this);
    b->setDefaultAction(action);
    b->setIconSize(QSize(28, 28));
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setObjectName(QStringLiteral("ribbon_") + action->objectName());
    m_buttons->addWidget(b);
    m_map.push_back({action, b});
}

void RibbonGroup::addSeparator() { m_caption->menu()->addSeparator(); }

QToolButton *RibbonGroup::buttonFor(QAction *action) const {
    for(const auto &[a, b] : m_map)
        if(a == action) return b;
    return nullptr;
}

RibbonTab::RibbonTab(QWidget *parent) : QWidget(parent) {
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 2, 6, 2);
    m_layout->setSpacing(2);
}

RibbonGroup *RibbonTab::addGroup(const QString &title) {
    if(m_layout->count() > 0) {
        auto *sep = new QFrame(this);
        sep->setFrameShape(QFrame::VLine);
        sep->setFrameShadow(QFrame::Plain);
        sep->setObjectName(QStringLiteral("ribbonSeparator"));
        m_layout->addWidget(sep);
    }
    auto *g = new RibbonGroup(title, this);
    m_layout->addWidget(g);
    return g;
}

void RibbonTab::addStretch() { m_layout->addStretch(1); }
void RibbonTab::addWidget(QWidget *w) { m_layout->addWidget(w); }

Ribbon::Ribbon(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("ribbon"));
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QStringLiteral(
        "#ribbon { background: #f4f5f7; border-bottom: 1px solid #c9ced6; }"
        "QToolButton { color: #1c2128; }"
        "QTabBar::tab { padding: 3px 14px; border: none; color: #3c4450; font-weight: 600; }"
        "QTabBar::tab:selected { color: #1a66c9; border-bottom: 2px solid #1a66c9; }"
        "QToolButton { border-radius: 3px; padding: 2px; }"
        "QToolButton:hover { background: rgba(40, 110, 200, 30); }"
        "QToolButton:checked { background: rgba(40, 110, 200, 60); }"
        "#ribbonGroupCaption, #fileMenuButton { background: transparent; border: none; padding: 0; }"
        "#ribbonGroupCaption::menu-indicator, #fileMenuButton::menu-indicator { image: none; width: 0; }"
        "#ribbonSeparator { color: #d4d8de; }"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_top = new QHBoxLayout;
    m_top->setContentsMargins(6, 2, 6, 0);
    m_top->setSpacing(4);
    m_tabs = new QTabBar(this);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_top->addWidget(m_tabs);
    m_top->addStretch(1);
    v->addLayout(m_top);
    m_stack = new QStackedWidget(this);
    v->addWidget(m_stack);
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int i) {
        if(i < 0) return;
        m_stack->setCurrentWidget(m_pages[size_t(m_tabs->tabData(i).toInt())]);
    });
}

void Ribbon::addLeadingWidget(QWidget *w) { m_top->insertWidget(m_top->indexOf(m_tabs), w); }

void Ribbon::addTrailingWidget(QWidget *w) { m_top->addWidget(w, 0, Qt::AlignVCenter); }

RibbonTab *Ribbon::addTab(const QString &name) {
    auto *t = new RibbonTab(this);
    t->setObjectName(name);
    m_pages.push_back(t);
    m_visible.push_back(true);
    m_stack->addWidget(t);
    rebuildTabs();
    return t;
}

void Ribbon::rebuildTabs() {
    QWidget *current = m_stack->currentWidget();
    const QSignalBlocker block(m_tabs);
    while(m_tabs->count()) m_tabs->removeTab(0);
    for(size_t i = 0; i < m_pages.size(); ++i) {
        if(!m_visible[i]) continue;
        const int idx = m_tabs->addTab(m_pages[i]->objectName());
        m_tabs->setTabData(idx, int(i));
        if(m_pages[i] == current) m_tabs->setCurrentIndex(idx);
    }
}

void Ribbon::setTabVisible(RibbonTab *tab, bool visible) {
    for(size_t i = 0; i < m_pages.size(); ++i)
        if(m_pages[i] == tab) m_visible[i] = visible;
    rebuildTabs();
}

void Ribbon::setCurrentTab(RibbonTab *tab) {
    m_stack->setCurrentWidget(tab);
    for(int i = 0; i < m_tabs->count(); ++i)
        if(m_pages[size_t(m_tabs->tabData(i).toInt())] == tab) {
            const QSignalBlocker block(m_tabs);
            m_tabs->setCurrentIndex(i);
        }
}

RibbonTab *Ribbon::currentTab() const { return static_cast<RibbonTab *>(m_stack->currentWidget()); }

} // namespace cadjitsu
