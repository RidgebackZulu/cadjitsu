#include "ui/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

namespace cadly {

void applyLightTheme(QApplication &app) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    QPalette p;
    const QColor text(28, 33, 40), window(244, 245, 247), base(Qt::white), disabled(150, 156, 166);
    const QColor highlight(26, 102, 201);
    for(QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool off = g == QPalette::Disabled;
        p.setColor(g, QPalette::Window, window);
        p.setColor(g, QPalette::WindowText, off ? disabled : text);
        p.setColor(g, QPalette::Base, base);
        p.setColor(g, QPalette::AlternateBase, QColor(238, 241, 245));
        p.setColor(g, QPalette::Text, off ? disabled : text);
        p.setColor(g, QPalette::PlaceholderText, disabled);
        p.setColor(g, QPalette::Button, QColor(250, 250, 251));
        p.setColor(g, QPalette::ButtonText, off ? disabled : text);
        p.setColor(g, QPalette::BrightText, Qt::white);
        p.setColor(g, QPalette::ToolTipBase, QColor(255, 255, 238));
        p.setColor(g, QPalette::ToolTipText, text);
        p.setColor(g, QPalette::Highlight, off ? QColor(200, 205, 212) : highlight);
        p.setColor(g, QPalette::HighlightedText, Qt::white);
        p.setColor(g, QPalette::Link, highlight);
        p.setColor(g, QPalette::Light, Qt::white);
        p.setColor(g, QPalette::Midlight, QColor(226, 229, 234));
        p.setColor(g, QPalette::Mid, QColor(190, 196, 204));
        p.setColor(g, QPalette::Dark, QColor(150, 156, 166));
        p.setColor(g, QPalette::Shadow, QColor(80, 86, 96));
    }
    QApplication::setPalette(p);
}

} // namespace cadly
