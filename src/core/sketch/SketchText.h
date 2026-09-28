#pragma once

#include "sketch/ProfileBuilder.h"
#include "sketch/Sketch.h"
#include "text/TextShape.h"

#include <string>
#include <vector>

namespace cad {

// Sketch text: its letters placed in the sketch (at the text's origin point,
// turned by its angle) and the sketch regions they make.

TextStyle textStyleOf(const SkEntity &text);

struct SketchTextLetters {
    bool ok = false;
    std::string error, warning;
    // Letter pieces: each an outer loop (counter-clockwise) then its holes,
    // in sketch coordinates.
    std::vector<std::vector<std::vector<Vec2>>> pieces;
};

SketchTextLetters sketchTextLetters(const Sketch &sketch, const SkEntity &text);

// Every region of the sketch: those its curves make (buildProfiles) and one
// per letter piece of its (non-construction) texts. A region holding a text
// has the letters as holes, and the insides of letters (an O's) become
// regions of their own. Letter regions have keys "t<text id>.<piece>".
std::vector<Profile> sketchProfiles(const Sketch &sketch);

// The id of the text a region belongs to (0 if none).
int textOfProfile(const Profile &profile);

} // namespace cad
