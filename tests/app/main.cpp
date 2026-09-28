#include "TestRegistry.h"
#include "ui/Fonts.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QtTest>

#include <memory>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <csignal>
#include <cstdio>
#include <execinfo.h>
#include <unistd.h>

namespace {

// A crash prints where it happened (QtTest's own handler only names the signal).
void printBacktrace(int sig) {
    void *frames[64];
    const int n = backtrace(frames, 64);
    std::fprintf(stderr, "\n*** crashed with signal %d; backtrace:\n", sig);
    std::fflush(stderr);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void installCrashBacktrace() {
    for(int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE}) std::signal(sig, printBacktrace);
}

} // namespace
#else
static void installCrashBacktrace() {}
#endif

std::vector<std::function<QObject *()>> &cadjitsuTestFactories() {
    static std::vector<std::function<QObject *()>> f;
    return f;
}

int main(int argc, char **argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    cadjitsu::applyLightTheme(app);
    // Settings the tests touch stay out of the user's own Cadjitsu settings.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("Cadjitsu Tests"));
    cadjitsu::registerBundledFonts();
    // CADJITSU_TEST_CLASS=SketchTests runs one test class (other arguments go to QtTest).
    const QByteArray only = qgetenv("CADJITSU_TEST_CLASS");
    // Our crash handler instead of QtTest's, for a backtrace.
    installCrashBacktrace();
    std::vector<char *> args(argv, argv + argc);
    char noCrashHandler[] = "-nocrashhandler";
    args.insert(args.begin() + 1, noCrashHandler);
    int failures = 0;
    for(const auto &make : cadjitsuTestFactories()) {
        std::unique_ptr<QObject> t(make());
        if(!only.isEmpty() && only != t->metaObject()->className()) continue;
        failures += QTest::qExec(t.get(), int(args.size()), args.data());
    }
    return failures;
}
