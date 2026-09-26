// Writes the app icon as a macOS .iconset (every size iconutil wants), or one
// PNG. Used by the macOS build to make Cadly.icns; needs no QGuiApplication.
#include "ui/AppIcon.h"

#include <QDir>
#include <QString>

#include <cstdio>

int main(int argc, char **argv) {
    if(argc < 2) {
        std::fprintf(stderr, "usage: cadly_icongen <dir.iconset> | <file.png> [size]\n");
        return 2;
    }
    const QString out = QString::fromLocal8Bit(argv[1]);
    if(out.endsWith(QLatin1String(".png"))) {
        const int size = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 1024;
        return cadly::appIconImage(size > 0 ? size : 1024).save(out) ? 0 : 1;
    }
    if(!QDir().mkpath(out)) return 1;
    const struct {
        int size;
        const char *name;
    } images[] = {{16, "icon_16x16.png"},      {32, "icon_16x16@2x.png"},   {32, "icon_32x32.png"},
                  {64, "icon_32x32@2x.png"},   {128, "icon_128x128.png"},   {256, "icon_128x128@2x.png"},
                  {256, "icon_256x256.png"},   {512, "icon_256x256@2x.png"}, {512, "icon_512x512.png"},
                  {1024, "icon_512x512@2x.png"}};
    for(const auto &im : images) {
        const QString path = QDir(out).filePath(QString::fromLatin1(im.name));
        if(!cadly::appIconImage(im.size).save(path)) {
            std::fprintf(stderr, "cadly_icongen: could not write %s\n", qPrintable(path));
            return 1;
        }
    }
    return 0;
}
