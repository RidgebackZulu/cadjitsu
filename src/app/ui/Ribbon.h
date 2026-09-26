#pragma once

#include <QString>
#include <QToolButton>
#include <QWidget>

#include <vector>

class QAction;
class QHBoxLayout;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace cadly {

// A button that drops down a menu (ribbon group captions, the File button):
// its label (and icon, if any) followed by a small drawn chevron, a soft
// rounded highlight on hover, and the chevron flipped up while the menu is
// open. Replaces Qt's menu-indicator arrow.
class MenuButton : public QToolButton {
    Q_OBJECT

public:
    enum class Style { Caption, Button };
    MenuButton(const QString &text, Style style, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
    bool isOpen() const { return m_open; }

    // The style sheet every Cadly drop-down menu uses.
    static QString menuStyleSheet();

protected:
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    QFont labelFont() const;
    Style m_style;
    bool m_hover = false;
    bool m_open = false;
};

// A group of tools on a ribbon tab ("CREATE", "MODIFY"...): large icon buttons
// with a caption underneath that drops down a menu listing every tool, like
// Fusion 360's toolbar.
class RibbonGroup : public QWidget {
    Q_OBJECT

public:
    RibbonGroup(const QString &title, QWidget *parent = nullptr);
    // `onBar` = show a button on the bar (otherwise only in the drop-down menu).
    void addAction(QAction *action, bool onBar = true);
    void addSeparator();
    QToolButton *buttonFor(QAction *action) const;
    MenuButton *caption() const { return m_caption; }

private:
    QHBoxLayout *m_buttons;
    MenuButton *m_caption;
    std::vector<std::pair<QAction *, QToolButton *>> m_map;
};

class RibbonTab : public QWidget {
    Q_OBJECT

public:
    explicit RibbonTab(QWidget *parent = nullptr);
    RibbonGroup *addGroup(const QString &title);
    void addStretch();
    void addWidget(QWidget *w);

private:
    QHBoxLayout *m_layout;
};

// Fusion-style toolbar: workspace tabs (SOLID, and the contextual SKETCH tab
// while editing a sketch) above rows of tool groups.
class Ribbon : public QWidget {
    Q_OBJECT

public:
    explicit Ribbon(QWidget *parent = nullptr);

    RibbonTab *addTab(const QString &name);
    void setTabVisible(RibbonTab *tab, bool visible);
    void setCurrentTab(RibbonTab *tab);
    RibbonTab *currentTab() const;
    // Widgets placed left of the tabs (file menu, undo / redo).
    void addLeadingWidget(QWidget *w);
    // At the right end of the top row (the MCP status button).
    void addTrailingWidget(QWidget *w);

private:
    QTabBar *m_tabs;
    QStackedWidget *m_stack;
    QHBoxLayout *m_top;
    std::vector<RibbonTab *> m_pages;
    std::vector<bool> m_visible;
    void rebuildTabs();
};

} // namespace cadly
