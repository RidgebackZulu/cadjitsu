#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "text/TextShape.h"

int main(int argc, char **argv) {
    // The fonts the app ships with, so text looks the same everywhere.
    cad::registerFontDirectory(CADJITSU_FONT_DIR);
    doctest::Context ctx;
    ctx.applyCommandLine(argc, argv);
    return ctx.run();
}
