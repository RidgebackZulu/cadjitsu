#pragma once

#include "base/Vec2.h"

#include <TopoDS_Shape.hxx>

#include <memory>
#include <string>
#include <vector>

namespace cad {

// Text as geometry: letters from a font as exact planar faces, and as
// closed 2D outlines for sketches and the canvas. Fonts come from the ones
// bundled with the app (registered with registerFontDirectory) and the
// system's; an unknown font falls back to DejaVu Sans with a warning.

struct TextStyle {
    std::string font = "DejaVu Sans";
    bool bold = false;
    bool italic = false;
    double size = 10.0;          // the font's em size (mm): capitals are about 0.73 of it
    double letterSpacing = 0.0;  // extra space between letters (mm)
    double lineSpacing = 1.2;    // baseline to baseline, times the size
    bool mirror = false;         // flipped left-right (reads from the other side)

    std::string key() const;
    bool operator==(const TextStyle &o) const { return key() == o.key(); }
};

struct TextShape {
    bool ok = false;
    std::string error;    // why there is no text
    std::string warning;  // e.g. the font was not found and another was used
    // The letters as planar faces on the XY plane (z = 0): the first line's
    // baseline starts at the origin and runs along +X, later lines below.
    TopoDS_Shape faces;
    // The same as closed outlines: outer loops counter-clockwise, holes clockwise.
    std::vector<std::vector<Vec2>> loops;
    Vec2 min, max;        // extent of the letters
};

// Builds (or returns the cached) text. Thread-safe. Lines split at '\n'.
std::shared_ptr<const TextShape> buildText(const std::string &utf8, const TextStyle &style);

// Makes the fonts in a folder (.ttf / .otf) available by their family names.
void registerFontDirectory(const std::string &dir);

// Font families: the registered (bundled) ones first, then the system's.
std::vector<std::string> availableFonts();

} // namespace cad
