#pragma once

class QDir;
class QTextStream;

namespace cadjitsu {

class MainWindow;

// The README's pictures: polished models built through the MCP tools and
// photographed in the app (the `showcase` selftest). Path-traced pictures take
// CADJITSU_SHOWCASE_SAMPLES samples per pixel (default 16; scripts/readme_images.sh
// renders them at full quality).
bool showcaseScenario(MainWindow &w, const QDir &out, QTextStream &log);

} // namespace cadjitsu
