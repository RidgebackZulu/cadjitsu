#pragma once

#include "base/Json.h"
#include "base/Status.h"
#include "doc/ModelState.h"
#include "topo/Refs.h"

#include <gp_Pln.hxx>

#include <string>

namespace cad {

// A section analysis (Inspect > Section Analysis): the model shown cut by a
// plane, to look inside it. It lives in the browser, not in the timeline, and
// follows the plane (or face) it is based on as the model changes.
struct SectionAnalysis {
    int id = 0;
    std::string name;    // "Section1"
    PlaneRef plane;
    double offset = 0.0; // along the plane's normal, mm
    bool flip = false;   // cut away the other side
    bool visible = true;

    json toJson() const;
    static SectionAnalysis fromJson(const json &j);
};

// The cutting plane in `state`: its normal points into the side that is cut away.
bool resolveSection(const ModelState &state, const SectionAnalysis &s, gp_Pln &plane, Status &status);

} // namespace cad
