#pragma once

#include <QColor>
#include <QObject>
#include <QString>

class QApplication;
class QWidget;

namespace cadjitsu {

// Light (as in Fusion 360, the default) or dark panels and canvas; System
// follows the computer's appearance. The app's colours are written for the
// light theme; in the dark one `themed` turns each chrome colour (greys and
// pale tints) to its dark counterpart and leaves the meaningful colours
// (selection blue, orange, red...) alone.
enum class ThemeMode { Light, Dark, System };

ThemeMode themeSetting(); // saved choice (ui/theme), Light by default
void setTheme(ThemeMode mode); // apply and save
void applyTheme(ThemeMode mode); // apply only
bool isDark();
QString themeName(ThemeMode mode); // "light", "dark", "system"

// The light palette the app always used, for start-up before settings are read.
void applyLightTheme(QApplication &app);

// `light` as it shows in the current theme.
QColor themed(const QColor &light);
inline QColor themed(int r, int g, int b, int a = 255) { return themed(QColor(r, g, b, a)); }
// A style sheet with each colour in it themed.
QString themedStyleSheet(const QString &light);
// Sets `w`'s style sheet from one written for the light theme, and again
// whenever the theme changes (later calls replace it).
void styled(QWidget *w, const QString &light);

// The canvas background and grid.
QColor canvasTop();
QColor canvasBottom();

// Emits changed() after the theme has changed (palette and style sheets are
// already updated; widgets that cache pictures redraw them).
class ThemeNotifier : public QObject {
    Q_OBJECT
public:
    static ThemeNotifier &instance();
signals:
    void changed();
};

} // namespace cadjitsu
