#pragma once

#include "base/Json.h"

#include <BRepTools_History.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>

#include <string>
#include <unordered_map>
#include <vector>

namespace cad {

// ---------------------------------------------------------------------------
// Topological naming.
//
// Every face of every body carries a persistent name derived from how it was
// created ("f7/side/c12.0": the side wall of extrude #7 swept from sketch
// segment c12.0). Names flow through later operations using OCCT's shape
// history; faces split into pieces get "~k" suffixes. Edges and vertices are
// named by the (sorted) names of their adjacent faces, e.g.
// "{f7/end/c4.0|f7/side/c12.0}", with "#k" to separate duplicates. Features
// store references by name (plus a geometric signature as a fallback), so
// they survive upstream parameter edits.

enum class TopoKind { Face, Edge, Vertex };

const char *toString(TopoKind k);
TopoKind topoKindFromString(const std::string &s);

// Geometric fingerprint used to re-find an entity when its name is gone.
struct GeomSig {
    int type = -1;          // 100 + GeomAbs surface type, 200 + curve type, 300 vertex; -1 unset
    gp_XYZ centroid;
    gp_XYZ dir;             // plane normal (outward) / axis / line direction
    double radius = 0.0;
    double measure = 0.0;   // area (faces) or length (edges)

    bool valid() const { return type >= 0; }
    json toJson() const;
    static GeomSig fromJson(const json &j);
};

GeomSig signatureOf(const TopoDS_Shape &shape);

// Dissimilarity of two signatures (0 = identical); +inf when incompatible.
// `scale` is a characteristic model size used to normalize distances.
double signatureDistance(const GeomSig &a, const GeomSig &b, double scale);

// Removes "~k" and "#k" disambiguation suffixes anywhere in a name.
std::string baseName(const std::string &name);

struct FaceLabel {
    std::string name;
    std::vector<std::string> aliases;
};

class NamedShape {
public:
    NamedShape() = default;
    // `labels` are aligned with the faces of TopExp::MapShapes(shape, TopAbs_FACE).
    NamedShape(const TopoDS_Shape &shape, std::vector<FaceLabel> labels);

    const TopoDS_Shape &shape() const { return m_shape; }
    bool isNull() const { return m_shape.IsNull(); }

    // Faces, edges and vertices use 1-based indices (0 = not found).
    int faceCount() const { return m_faces.Extent(); }
    int edgeCount() const { return m_edges.Extent(); }
    int vertexCount() const { return m_vertices.Extent(); }
    int count(TopoKind k) const;

    TopoDS_Face face(int i) const;
    TopoDS_Edge edge(int i) const;
    TopoDS_Vertex vertex(int i) const;
    TopoDS_Shape shapeOf(TopoKind k, int i) const;

    const std::string &faceName(int i) const { return m_faceNames[i - 1]; }
    const std::string &edgeName(int i) const { return m_edgeNames[i - 1]; }
    const std::string &vertexName(int i) const { return m_vertexNames[i - 1]; }
    const std::string &nameOf(TopoKind k, int i) const;
    const std::vector<std::string> &faceAliases(int i) const { return m_faceAliases[i - 1]; }

    int indexOf(TopoKind k, const TopoDS_Shape &s) const;
    int indexOfName(TopoKind k, const std::string &name) const;
    std::vector<int> indicesWithAlias(const std::string &alias) const; // faces only
    std::vector<int> indicesWithBaseName(TopoKind k, const std::string &base) const;

    // Seam and degenerate edges are not user-selectable.
    bool isSelectableEdge(int i) const;

    // The names of the faces of `part` (a solid inside this shape), keeping names.
    NamedShape subShape(const TopoDS_Shape &part) const;

    std::vector<FaceLabel> faceLabels() const;

private:
    void build(std::vector<FaceLabel> labels);

    TopoDS_Shape m_shape;
    TopTools_IndexedMapOfShape m_faces, m_edges, m_vertices;
    std::vector<std::string> m_faceNames, m_edgeNames, m_vertexNames;
    std::vector<std::vector<std::string>> m_faceAliases;
    std::vector<char> m_edgeSelectable;
    std::unordered_map<std::string, int> m_faceByName, m_edgeByName, m_vertexByName;
};

// Carries face names from `inputs` to `result` through `history`:
//  - faces kept or modified inherit their input name (merged faces keep the
//    smallest name, the others become aliases);
//  - faces generated from an input edge / vertex / face are named
//    "<prefix>/<source name>" (vertex sources are prefixed "v:", faces "f:");
//  - anything else becomes "<prefix>/new".
NamedShape propagateNames(const TopoDS_Shape &result, const std::vector<const NamedShape *> &inputs,
                          const Handle(BRepTools_History) & history, const std::string &prefix);

} // namespace cad
