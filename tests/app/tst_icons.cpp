// The icon set: every icon has an SVG that renders, sharp at any size and
// device pixel ratio, with recolourable accents and a disabled look.
#include "TestRegistry.h"

#include "ui/Icons.h"

#include <QFile>
#include <QPixmap>
#include <QtTest>

#include <set>

using namespace cadly;

namespace {

int opaquePixels(const QImage &img) {
    int n = 0;
    for(int y = 0; y < img.height(); ++y)
        for(int x = 0; x < img.width(); ++x) n += qAlpha(img.pixel(x, y)) > 128;
    return n;
}

} // namespace

class IconTests : public QObject {
    Q_OBJECT

private slots:
    void everyIconHasItsOwnSvg() {
        std::set<QString> names;
        for(IconId id : allIcons()) {
            const QString name = iconName(id);
            QVERIFY2(!name.isEmpty(), "every IconId has a name");
            QVERIFY2(names.insert(name).second, qPrintable(name + " is used twice"));
            QVERIFY2(QFile::exists(QStringLiteral(":/icons/%1.svg").arg(name)), qPrintable(name));
            // The file in the source tree matches (the generator was run).
            QVERIFY2(QFile::exists(QStringLiteral(CADLY_SOURCE_DIR "/resources/icons/%1.svg").arg(name)), qPrintable(name));
        }
        QCOMPARE(int(names.size()), int(IconId::Repeat) + 1);
    }

    void everyIconRendersAtSmallAndLargeSizes() {
        for(IconId id : allIcons()) {
            const QImage small = iconImage(id, 16), large = iconImage(id, 128);
            QCOMPARE(small.size(), QSize(16, 16));
            QVERIFY2(opaquePixels(small) >= 12, qPrintable(iconName(id) + " at 16 px"));
            QVERIFY2(opaquePixels(large) >= 12 * 64, qPrintable(iconName(id) + " at 128 px"));
        }
    }

    void pixmapsAreDrawnAtTheDevicePixelRatio() {
        const QPixmap pm = icon(IconId::Extrude).pixmap(QSize(24, 24), 2.0);
        QCOMPARE(pm.size(), QSize(48, 48));
        QCOMPARE(pm.devicePixelRatio(), 2.0);
        // Not an upscaled 24 px image: the 48 px render has finer detail.
        QImage drawn = pm.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        drawn.setDevicePixelRatio(1.0);
        QCOMPARE(drawn, iconImage(IconId::Extrude, 48).convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }

    void theAccentColourIsSwappedIn() {
        const QImage blue = iconImage(IconId::Parallel, 32);
        const QImage red = iconImage(IconId::Parallel, 32, QColor(220, 40, 30));
        QVERIFY(blue != red);
        long r = 0, b = 0;
        for(int y = 0; y < 32; ++y)
            for(int x = 0; x < 32; ++x) {
                r += qRed(red.pixel(x, y));
                b += qBlue(red.pixel(x, y));
            }
        QVERIFY(r > b);
    }

    void disabledIconsAreGreyAndFaded() {
        const QImage normal = icon(IconId::Extrude).pixmap(QSize(32, 32), 1.0, QIcon::Normal).toImage();
        const QImage off = icon(IconId::Extrude).pixmap(QSize(32, 32), 1.0, QIcon::Disabled).toImage();
        QVERIFY(normal != off);
        int coloured = 0;
        for(int y = 0; y < 32; ++y)
            for(int x = 0; x < 32; ++x) {
                const QColor c = off.pixelColor(x, y);
                if(c.alpha() > 200) ++coloured;
                if(c.alpha() > 0) QVERIFY(std::abs(c.red() - c.blue()) <= 2);
            }
        QCOMPARE(coloured, 0);
    }
};

CADLY_REGISTER_TEST(IconTests)

#include "tst_icons.moc"
