#include "ui/Fonts.h"

#include "text/TextShape.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace cadjitsu {

QString registerBundledFonts() {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {app + QStringLiteral("/../Resources/fonts"), app + QStringLiteral("/fonts"),
                                    QStringLiteral(CADJITSU_FONT_DIR)};
    for(const QString &dir : candidates) {
        if(!QFileInfo::exists(dir + QStringLiteral("/DejaVuSans.ttf"))) continue;
        const QString path = QDir(dir).canonicalPath();
        cad::registerFontDirectory(QDir::toNativeSeparators(path).toStdString());
        return path;
    }
    return {};
}

} // namespace cadjitsu
