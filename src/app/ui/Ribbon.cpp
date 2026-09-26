#include "ui/Ribbon.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace cadly {

RibbonGroup::RibbonGroup(const QString &title, QWidget *parent) : QWidget(parent) {
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(4, 2, 4, 0);
    v->setSpacing(0);
    m_buttons = new QHBoxLayout;
    m_buttons->setSpacing(1);
    v->addLayout(m_buttons);
    m_caption = new QToolButton(this);
    m_caption->setText(title + QStringLiteral(" ▾"));
    m_caption->setObjectName(QStringLiteral("ribbonGroupCaption"));
    m_caption->setPopupMode(QToolButton::InstantPopup);
    m_caption->setAutoRaise(true);
    m_caption->setMenu(new QMenu(m_caption));
    m_caption->setFocusPolicy(Qt::NoFocus);
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
        "QTabBar::tab { padding: 3px 14px; border: none; color: #3c4450; font-weight: 600; }"
        "QTabBar::tab:selected { color: #1a66c9; border-bottom: 2px solid #1a66c9; }"
        "QToolButton { border-radius: 3px; padding: 2px; }"
        "QToolButton:hover { background: rgba(40, 110, 200, 30); }"
        "QToolButton:checked { background: rgba(40, 110, 200, 60); }"
        "#ribbonGroupCaption { font-size: 10px; color: #4a5260; font-weight: 600; padding: 0 2px; }"
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

} // namespace cadly
