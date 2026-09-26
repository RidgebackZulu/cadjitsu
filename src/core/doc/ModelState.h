#pragma once

#include "base/Ids.h"
#include "sketch/SketchResult.h"
#include "topo/NamedShape.h"

#include <gp_Ax3.hxx>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cad {

struct MeshData;

// A solid body with named topology.
struct Body {
    BodyId id;
    std::string name; // default display name ("Body1"); user renames live in the Document
    int order = 0;    // creation order, for stable display
    FeatureId createdBy = kNoFeature;
    NamedShape shape;

    // Display tessellation, built on first use and shared by every model state
    // that contains this (immutable) body.
    std::shared_ptr<const MeshData> mesh(double deflection = 0.0) const;

private:
    mutable std::once_flag m_meshOnce;
    mutable std::shared_ptr<const MeshData> m_mesh;
};

// A construction plane produced by a ConstructionPlane feature.
struct PlaneResult {
    FeatureId feature = kNoFeature;
    std::string name;
    gp_Ax3 frame;
    double halfSize = 50.0; // display extent
};

// The complete geometric state after applying a prefix of the timeline.
// Immutable once published; later features copy the maps (bodies are shared).
struct ModelState {
    std::map<BodyId, std::shared_ptr<const Body>> bodies;
    std::map<FeatureId, std::shared_ptr<const SketchResult>> sketches;
    std::map<FeatureId, std::shared_ptr<const PlaneResult>> planes;
    std::map<BodyId, BodyId> mergedInto; // bodies absorbed by joins
    int bodyCounter = 0;                  // for "BodyN" default names
    int bodyOrder = 0;

    // Follows join history to the body that now contains `id` ("" if gone).
    BodyId resolveBodyId(BodyId id) const;
    const Body *body(const BodyId &id) const;
    std::vector<const Body *> orderedBodies() const;
    double modelSize() const; // bounding diagonal of all bodies (>= 1)
};

using StatePtr = std::shared_ptr<const ModelState>;

} // namespace cad
