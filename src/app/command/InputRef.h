#pragma once

#include "model/ModelView.h"
#include "model/Selection.h"

#include "doc/ModelState.h"
#include "topo/Refs.h"

#include <QColor>

#include <optional>
#include <string>
#include <vector>

namespace cadly {

// A model entity picked for a command input. It is stored by name or id (not
// by index), so it survives the preview changing the model around it, and it
// is resolved in the model the command starts from.
struct InputRef {
    SelectionItem::Kind kind = SelectionItem::Kind::Face;
    cad::TopoRef topo;                        // Face / Edge / Vertex
    cad::BodyId body;                         // Body
    cad::ProfileRef profile;                  // Profile
    cad::FeatureId feature = cad::kNoFeature; // Plane: construction plane; SketchEntity: its sketch
    std::string key;                          // Plane: origin plane ("XY", "XZ", "YZ")
    int entity = 0;                           // SketchEntity: the point

    bool operator==(const InputRef &o) const; // the same entity

    static InputRef ofTopo(const cad::TopoRef &r);
    static InputRef ofBody(const cad::BodyId &id);
    static InputRef ofProfile(const cad::ProfileRef &p);
    static InputRef ofPlane(const cad::PlaneRef &p);
    static InputRef ofSketchPoint(cad::FeatureId sketch, int point);

    bool isTopo() const;
    // Plane and planar Face references as a plane.
    std::optional<cad::PlaneRef> planeRef() const;
    // Made by feature `self` (a preview's own faces and edges are not inputs).
    bool createdBy(cad::FeatureId self) const;
};

// The reference for something picked in `view` (in the model it shows).
std::optional<InputRef> inputRefOf(const ModelView &view, const SelectionItem &item);

// Adds `r` (or removes it if it is there already). Returns true if added.
bool toggleRef(std::vector<InputRef> &refs, const InputRef &r);

// Draws references resolved in `base` (the model before the command's
// feature) as marks tagged `tag`. Returns false if `r` cannot be found.
bool markInput(ModelView &view, const cad::ModelState &base, const InputRef &r, int tag, const QColor &color,
               ModelView::InputMarks &marks);

} // namespace cadly
