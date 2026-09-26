#include "MainWindow.h"
#include "selftest/SelfTest.h"
#include "mcp/McpTools.h"
#include "ui/AppIcon.h"
#include "ui/Theme.h"
#include "viewport/Viewport.h"

#include "base/Version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>

namespace {

QRhiWidget::Api parseApi(const QString &name, bool *ok) {
    *ok = true;
    const QString n = name.toLower();
    if(n == QLatin1String("metal")) return QRhiWidget::Api::Metal;
    if(n == QLatin1String("opengl") || n == QLatin1String("gl")) return QRhiWidget::Api::OpenGL;
    if(n == QLatin1String("vulkan")) return QRhiWidget::Api::Vulkan;
    if(n == QLatin1String("d3d11")) return QRhiWidget::Api::Direct3D11;
    if(n == QLatin1String("null")) return QRhiWidget::Api::Null;
    *ok = false;
    return QRhiWidget::Api::OpenGL;
}

// Finder opens .cadly documents (double-click, drop on the Dock icon) with a
// FileOpen event to the application.
class FileOpenFilter : public QObject {
public:
    explicit FileOpenFilter(cadly::MainWindow &window) : m_window(window) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if(event->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent *>(event)->file();
            if(!path.isEmpty()) m_window.openFile(path);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    cadly::MainWindow &m_window;
};

} // namespace

int main(int argc, char *argv[]) {
    // Used when the viewport runs on OpenGL (Linux, or --rhi=opengl on macOS).
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    cadly::applyLightTheme(app);
    QApplication::setApplicationName(QStringLiteral("Cadly"));
    QApplication::setApplicationVersion(QString::fromLatin1(cad::version()));
    QApplication::setOrganizationName(QStringLiteral("Cadly"));
    QApplication::setWindowIcon(cadly::appIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Parametric CAD for 3D-printable parts"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selfTestOpt(QStringLiteral("selftest"),
                                   QStringLiteral("Run a scripted self test and exit."),
                                   QStringLiteral("name"));
    QCommandLineOption outOpt(QStringLiteral("out"),
                              QStringLiteral("Output directory for self-test artifacts."),
                              QStringLiteral("dir"));
    QCommandLineOption rhiOpt(QStringLiteral("rhi"),
                              QStringLiteral("Graphics API: metal, opengl, vulkan, d3d11, null."),
                              QStringLiteral("api"));
    QCommandLineOption listOpt(QStringLiteral("list-selftests"),
                               QStringLiteral("Print the available self tests and exit."));
    QCommandLineOption toolsOpt(QStringLiteral("mcp-tools"),
                                QStringLiteral("Print the MCP server's tool list (JSON) and exit."));
    parser.addOptions({selfTestOpt, outOpt, rhiOpt, listOpt, toolsOpt});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A .cadly design to open."), QStringLiteral("[file]"));
    parser.process(app);

    if(parser.isSet(listOpt)) {
        for(const QString &name : cadly::selfTestNames()) printf("%s\n", qPrintable(name));
        return 0;
    }

    cadly::MainWindow window;
    if(parser.isSet(toolsOpt)) {
        printf("%s\n", window.mcpTools()->toolList().dump(2).c_str());
        return 0;
    }
    if(parser.isSet(rhiOpt)) {
        bool ok = false;
        const auto api = parseApi(parser.value(rhiOpt), &ok);
        if(!ok) {
            qCritical("Unknown --rhi value '%s'", qPrintable(parser.value(rhiOpt)));
            return 2;
        }
        window.viewport()->setApi(api);
    }
    FileOpenFilter fileOpen(window);
    app.installEventFilter(&fileOpen);
    window.show();
    if(!parser.isSet(selfTestOpt) && !parser.positionalArguments().isEmpty())
        QTimer::singleShot(0, &window, [&] { window.openFile(parser.positionalArguments().constFirst()); });

    if(parser.isSet(selfTestOpt)) {
        int code = 0;
        QTimer::singleShot(0, &app, [&] {
            code = cadly::runSelfTest(window, parser.value(selfTestOpt), parser.value(outOpt));
            app.exit(code);
        });
        app.exec();
        return code;
    }
    return app.exec();
}
