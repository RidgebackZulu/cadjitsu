#include "ui/Theme.h"

#include <QApplication>
#include <QHash>
#include <QPalette>
#include <QPainter>
#include <QPixmapCache>
#include <QProxyStyle>
#include <QStyleOption>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace cadjitsu {

namespace {

bool g_dark = false;
ThemeMode g_mode = ThemeMode::Light;
bool g_watching = false;

// Widgets styled with `styled`, with the light style sheet each was given.
QHash<QWidget *, QString> &styledWidgets() {
    static QHash<QWidget *, QString> h;
    return h;
}

QPalette lightPalette() {
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
    return p;
}

// The light palette with its greys turned dark; the accents stay.
QPalette darkPalette() {
    QPalette p = lightPalette();
    for(QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
        for(int r = 0; r < QPalette::NColorRoles; ++r) {
            const auto role = QPalette::ColorRole(r);
            if(role == QPalette::HighlightedText || role == QPalette::BrightText || role == QPalette::Highlight ||
               role == QPalette::Link || role == QPalette::NoRole)
                continue;
            p.setColor(g, role, themed(p.color(g, role)));
        }
    // Shadow stays dark (it is drawn under things).
    for(QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(g, QPalette::Shadow, QColor(10, 12, 15));
        p.setColor(g, QPalette::ToolTipBase, QColor(52, 57, 66));
        p.setColor(g, QPalette::Highlight, g == QPalette::Disabled ? QColor(70, 76, 86) : QColor(40, 112, 214));
        p.setColor(g, QPalette::Link, QColor(110, 170, 245));
    }
    return p;
}

bool systemIsDark() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

void watchSystem() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if(g_watching || !qApp) return;
    g_watching = true;
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [](Qt::ColorScheme) {
        if(g_mode == ThemeMode::System && systemIsDark() != g_dark) applyTheme(ThemeMode::System);
    });
#endif
}

// Check boxes and radio buttons with a light rim in the dark theme (the
// platform style draws theirs dark, lost on dark panels).
class ThemeStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p, const QWidget *w) const override {
        QProxyStyle::drawPrimitive(pe, opt, p, w);
        if(!g_dark || (pe != PE_IndicatorCheckBox && pe != PE_IndicatorRadioButton)) return;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const bool on = opt->state & (State_On | State_NoChange);
        const bool enabled = opt->state & State_Enabled;
        p->setPen(QPen(on && enabled ? QColor(70, 140, 235) : QColor(150, 160, 174, enabled ? 255 : 110), 1.0));
        p->setBrush(Qt::NoBrush);
        const QRectF r = QRectF(opt->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        if(pe == PE_IndicatorRadioButton) p->drawEllipse(r);
        else p->drawRoundedRect(r, 2.5, 2.5);
        p->restore();
    }
};

void installStyle() {
    static bool done = false;
    if(done || !qApp) return;
    done = true;
    QApplication::setStyle(new ThemeStyle(QApplication::style()->name()));
}

QString cssColor(const QColor &c) {
    if(c.alpha() == 255) return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

} // namespace

ThemeNotifier &ThemeNotifier::instance() {
    static ThemeNotifier n;
    return n;
}

QString themeName(ThemeMode mode) {
    switch(mode) {
    case ThemeMode::Dark: return QStringLiteral("dark");
    case ThemeMode::System: return QStringLiteral("system");
    default: return QStringLiteral("light");
    }
}

ThemeMode themeSetting() {
    const QString s = QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString();
    return s == QLatin1String("dark") ? ThemeMode::Dark : s == QLatin1String("system") ? ThemeMode::System : ThemeMode::Light;
}

bool isDark() { return g_dark; }

void applyLightTheme(QApplication &) {
    g_dark = false;
    g_mode = ThemeMode::Light;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
#endif
    QApplication::setPalette(lightPalette());
}

void applyTheme(ThemeMode mode) {
    g_mode = mode;
    watchSystem();
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Light or dark: the system's own widgets (file dialogs) match; System: let it decide.
    QGuiApplication::styleHints()->setColorScheme(mode == ThemeMode::Dark    ? Qt::ColorScheme::Dark
                                                  : mode == ThemeMode::Light ? Qt::ColorScheme::Light
                                                                             : Qt::ColorScheme::Unknown);
#endif
    const bool dark = mode == ThemeMode::Dark || (mode == ThemeMode::System && systemIsDark());
    if(dark) installStyle();
    const bool changed = dark != g_dark;
    g_dark = dark;
    QApplication::setPalette(dark ? darkPalette() : lightPalette());
    if(!changed) return;
    for(auto it = styledWidgets().cbegin(); it != styledWidgets().cend(); ++it) it.key()->setStyleSheet(themedStyleSheet(it.value()));
    QPixmapCache::clear();
    emit ThemeNotifier::instance().changed();
    for(QWidget *w : QApplication::allWidgets()) w->update();
}

void setTheme(ThemeMode mode) {
    QSettings().setValue(QStringLiteral("ui/theme"), themeName(mode));
    applyTheme(mode);
}

QColor themed(const QColor &light) {
    if(!g_dark || !light.isValid()) return light;
    const int hi = std::max({light.red(), light.green(), light.blue()});
    const int lo = std::min({light.red(), light.green(), light.blue()});
    // Strong colours mean something (selection, errors, axes): they stay.
    // (Pale tints, e.g. a light blue selection behind text, count as chrome.)
    if(hi - lo >= 48 && !(light.lightnessF() > 0.78 && hi - lo < 110)) return light;
    // Greys and pale tints: light becomes dark and dark light, keeping the hue.
    const double l = light.lightnessF();
    const double nl = std::clamp(0.90 - 0.74 * l, 0.0, 1.0);
    float h, s, ll, a;
    light.getHslF(&h, &s, &ll, &a);
    QColor out = QColor::fromHslF(h < 0 ? 0.6f : h, std::min(s, 0.16f), float(nl), a);
    return out;
}

QString themedStyleSheet(const QString &sheet) {
    if(!g_dark) return sheet;
    // White panels and fields, by name (white text on a button stays white).
    QString light = sheet;
    light.replace(QStringLiteral("background: white"), QStringLiteral("background: #ffffff"));
    static const QRegularExpression re(QStringLiteral(
        R"(#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b|rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:,\s*(\d+(?:\.\d+)?)\s*)?\))"));
    QString out;
    out.reserve(light.size());
    qsizetype last = 0;
    const bool hasBlocks = light.contains(QLatin1Char('{'));
    for(auto it = re.globalMatch(light); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        QColor c;
        if(!m.captured(1).isEmpty()) {
            // Colours are inside { }: a "#add" outside is an id selector.
            if(hasBlocks) {
                const QStringView head = QStringView(light).first(m.capturedStart());
                if(head.count(QLatin1Char('{')) <= head.count(QLatin1Char('}'))) continue;
            }
            c = QColor(QLatin1Char('#') + m.captured(1));
        } else {
            c = QColor(m.captured(2).toInt(), m.captured(3).toInt(), m.captured(4).toInt());
            if(!m.captured(5).isEmpty()) {
                const double a = m.captured(5).toDouble();
                c.setAlpha(a <= 1.0 && m.captured(5).contains('.') ? int(std::lround(a * 255)) : int(a));
            }
        }
        if(!c.isValid()) continue;
        out += light.mid(last, m.capturedStart() - last);
        out += cssColor(themed(c));
        last = m.capturedEnd();
    }
    out += light.mid(last);
    return out;
}

void styled(QWidget *w, const QString &light) {
    if(!w) return;
    if(!styledWidgets().contains(w))
        QObject::connect(w, &QObject::destroyed, [w] { styledWidgets().remove(w); });
    styledWidgets()[w] = light;
    w->setStyleSheet(themedStyleSheet(light));
}

QColor canvasTop() { return g_dark ? QColor(78, 84, 94) : QColor(250, 251, 252); }
QColor canvasBottom() { return g_dark ? QColor(38, 42, 48) : QColor(196, 205, 216); }

} // namespace cadjitsu
