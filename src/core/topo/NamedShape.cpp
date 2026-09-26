#include "topo/NamedShape.h"

#include "geom/OcctUtil.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace cad {

namespace {

const std::string kEmpty;

// GeomSig::type encodes the entity kind so face and edge types never collide.
constexpr int kFaceTypeBase = 100;
constexpr int kEdgeTypeBase = 200;
constexpr int kVertexType = 300;

struct SortKey {
    double x, y, z, m;
};

bool keyLess(const SortKey &a, const SortKey &b) {
    const double tol = 1e-6;
    if(std::fabs(a.x - b.x) > tol) return a.x < b.x;
    if(std::fabs(a.y - b.y) > tol) return a.y < b.y;
    if(std::fabs(a.z - b.z) > tol) return a.z < b.z;
    return a.m < b.m;
}

SortKey faceKey(const TopoDS_Face &f) {
    GProp_GProps props;
    BRepGProp::SurfaceProperties(f, props);
    const gp_Pnt c = props.Mass() > 0 ? props.CentreOfMass() : centroidOfFace(f);
    return {c.X(), c.Y(), c.Z(), props.Mass()};
}

SortKey edgeKey(const TopoDS_Edge &e) {
    const gp_Pnt m = midpointOfEdge(e);
    return {m.X(), m.Y(), m.Z(), BRep_Tool::Degenerated(e) ? 0.0 : lengthOf(e)};
}

SortKey vertexKey(const TopoDS_Vertex &v) {
    const gp_Pnt p = BRep_Tool::Pnt(v);
    return {p.X(), p.Y(), p.Z(), 0.0};
}

// Appends "<sep><k>" to every member of groups sharing a name, ordered by geometry.
template <typename KeyFn>
void disambiguate(std::vector<std::string> &names, const TopTools_IndexedMapOfShape &map, const char *sep,
                  KeyFn keyOf) {
    std::map<std::string, std::vector<int>> groups;
    for(size_t i = 0; i < names.size(); ++i) groups[names[i]].push_back(int(i));
    for(auto &[name, idx] : groups) {
        if(idx.size() < 2) continue;
        std::vector<std::pair<SortKey, int>> keyed;
        for(int i : idx) keyed.push_back({keyOf(map(i + 1)), i});
        std::stable_sort(keyed.begin(), keyed.end(),
                         [](const auto &a, const auto &b) { return keyLess(a.first, b.first); });
        for(size_t k = 0; k < keyed.size(); ++k) names[keyed[k].second] = name + sep + std::to_string(k + 1);
    }
}

std::string joinSorted(std::vector<std::string> parts, const char *open, const char *sep, const char *close) {
    std::sort(parts.begin(), parts.end());
    parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
    std::string out = open;
    for(size_t i = 0; i < parts.size(); ++i) {
        if(i) out += sep;
        out += parts[i];
    }
    return out + close;
}

} // namespace

// ---------------------------------------------------------------------------

const char *toString(TopoKind k) {
    switch(k) {
    case TopoKind::Face: return "face";
    case TopoKind::Edge: return "edge";
    case TopoKind::Vertex: return "vertex";
    }
    return "face";
}

TopoKind topoKindFromString(const std::string &s) {
    if(s == "edge") return TopoKind::Edge;
    if(s == "vertex") return TopoKind::Vertex;
    return TopoKind::Face;
}

json GeomSig::toJson() const {
    return json{{"type", type}, {"c", cad::toJson(centroid)}, {"d", cad::toJson(dir)}, {"r", radius}, {"m", measure}};
}

GeomSig GeomSig::fromJson(const json &j) {
    GeomSig s;
    if(!j.is_object()) return s;
    s.type = jget<int>(j, "type", -1);
    s.centroid = xyzFromJson(j.value("c", json()));
    s.dir = xyzFromJson(j.value("d", json()));
    s.radius = jget<double>(j, "r", 0.0);
    s.measure = jget<double>(j, "m", 0.0);
    return s;
}

GeomSig signatureOf(const TopoDS_Shape &shape) {
    GeomSig s;
    if(shape.IsNull()) return s;
    if(shape.ShapeType() == TopAbs_FACE) {
        const TopoDS_Face f = TopoDS::Face(shape);
        BRepAdaptor_Surface surf(f, false);
        s.type = kFaceTypeBase + int(surf.GetType());
        GProp_GProps props;
        BRepGProp::SurfaceProperties(f, props);
        s.measure = props.Mass();
        s.centroid = (props.Mass() > 0 ? props.CentreOfMass() : centroidOfFace(f)).XYZ();
        switch(surf.GetType()) {
        case GeomAbs_Plane: {
            gp_Pln pln;
            planeOfFace(f, pln);
            s.dir = pln.Axis().Direction().XYZ();
            break;
        }
        case GeomAbs_Cylinder:
            s.dir = surf.Cylinder().Axis().Direction().XYZ();
            s.radius = surf.Cylinder().Radius();
            break;
        case GeomAbs_Cone:
            s.dir = surf.Cone().Axis().Direction().XYZ();
            s.radius = surf.Cone().RefRadius();
            break;
        case GeomAbs_Sphere:
            s.radius = surf.Sphere().Radius();
            break;
        case GeomAbs_Torus:
            s.dir = surf.Torus().Axis().Direction().XYZ();
            s.radius = surf.Torus().MinorRadius();
            break;
        default:
            break;
        }
    } else if(shape.ShapeType() == TopAbs_EDGE) {
        const TopoDS_Edge e = TopoDS::Edge(shape);
        if(BRep_Tool::Degenerated(e)) {
            s.type = kEdgeTypeBase + 99;
            s.centroid = midpointOfEdge(e).XYZ();
            return s;
        }
        BRepAdaptor_Curve c(e);
        s.type = kEdgeTypeBase + int(c.GetType());
        s.measure = lengthOf(e);
        s.centroid = midpointOfEdge(e).XYZ();
        if(c.GetType() == GeomAbs_Line) {
            s.dir = c.Line().Direction().XYZ();
        } else if(c.GetType() == GeomAbs_Circle) {
            s.dir = c.Circle().Axis().Direction().XYZ();
            s.radius = c.Circle().Radius();
        }
    } else if(shape.ShapeType() == TopAbs_VERTEX) {
        s.type = kVertexType;
        s.centroid = BRep_Tool::Pnt(TopoDS::Vertex(shape)).XYZ();
    }
    return s;
}

double signatureDistance(const GeomSig &a, const GeomSig &b, double scale) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    if(!a.valid() || !b.valid() || a.type != b.type) return inf;
    scale = std::max(scale, 1e-6);
    double score = (a.centroid - b.centroid).Modulus() / scale;
    const double na = a.dir.Modulus(), nb = b.dir.Modulus();
    if(na > 1e-9 && nb > 1e-9) {
        double cosang = a.dir.Dot(b.dir) / (na * nb);
        // Plane normals are oriented (top vs bottom face); axes and lines are not.
        if(a.type != kFaceTypeBase + int(GeomAbs_Plane)) cosang = std::fabs(cosang);
        cosang = std::clamp(cosang, -1.0, 1.0);
        score += std::acos(cosang) * 2.0;
    }
    if(a.radius > 0 || b.radius > 0) {
        const double r = std::max(a.radius, b.radius);
        score += std::fabs(a.radius - b.radius) / std::max(r, 1e-9);
    }
    if(a.measure > 0 && b.measure > 0) score += 0.25 * std::fabs(std::log(a.measure / b.measure));
    return score;
}

std::string baseName(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for(size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if((c == '~' || c == '#') && i + 1 < name.size() && std::isdigit((unsigned char)name[i + 1])) {
            ++i;
            while(i + 1 < name.size() && std::isdigit((unsigned char)name[i + 1])) ++i;
            continue;
        }
        out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------

NamedShape::NamedShape(const TopoDS_Shape &shape, std::vector<FaceLabel> labels) : m_shape(shape) {
    build(std::move(labels));
}

void NamedShape::build(std::vector<FaceLabel> labels) {
    m_faces.Clear();
    m_edges.Clear();
    m_vertices.Clear();
    if(m_shape.IsNull()) return;
    TopExp::MapShapes(m_shape, TopAbs_FACE, m_faces);
    TopExp::MapShapes(m_shape, TopAbs_EDGE, m_edges);
    TopExp::MapShapes(m_shape, TopAbs_VERTEX, m_vertices);

    const int nf = m_faces.Extent();
    labels.resize(size_t(nf));
    m_faceNames.resize(size_t(nf));
    m_faceAliases.resize(size_t(nf));
    for(int i = 0; i < nf; ++i) {
        m_faceNames[i] = labels[i].name.empty() ? "unnamed" : labels[i].name;
        m_faceAliases[i] = std::move(labels[i].aliases);
    }
    disambiguate(m_faceNames, m_faces, "~", [](const TopoDS_Shape &s) { return faceKey(TopoDS::Face(s)); });
    m_faceByName.clear();
    for(int i = 0; i < nf; ++i) m_faceByName[m_faceNames[i]] = i + 1;

    // Edges: named by their adjacent faces.
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(m_shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    const int ne = m_edges.Extent();
    m_edgeNames.assign(size_t(ne), std::string());
    m_edgeSelectable.assign(size_t(ne), 1);
    for(int i = 1; i <= ne; ++i) {
        const TopoDS_Edge e = TopoDS::Edge(m_edges(i));
        std::vector<std::string> parts;
        std::set<int> faceIdx;
        const int k = edgeFaces.FindIndex(e);
        if(k > 0) {
            for(TopTools_ListOfShape::Iterator it(edgeFaces(k)); it.More(); it.Next()) {
                const int fi = m_faces.FindIndex(it.Value());
                if(fi > 0 && faceIdx.insert(fi).second) parts.push_back(m_faceNames[fi - 1]);
            }
        }
        m_edgeNames[i - 1] = joinSorted(parts, "{", "|", "}");
        if(BRep_Tool::Degenerated(e) || faceIdx.size() < 2) m_edgeSelectable[i - 1] = 0;
    }
    disambiguate(m_edgeNames, m_edges, "#", [](const TopoDS_Shape &s) { return edgeKey(TopoDS::Edge(s)); });
    m_edgeByName.clear();
    for(int i = 0; i < ne; ++i) m_edgeByName[m_edgeNames[i]] = i + 1;

    TopTools_IndexedDataMapOfShapeListOfShape vertexFaces;
    TopExp::MapShapesAndAncestors(m_shape, TopAbs_VERTEX, TopAbs_FACE, vertexFaces);
    const int nv = m_vertices.Extent();
    m_vertexNames.assign(size_t(nv), std::string());
    for(int i = 1; i <= nv; ++i) {
        std::vector<std::string> parts;
        const int k = vertexFaces.FindIndex(m_vertices(i));
        if(k > 0) {
            for(TopTools_ListOfShape::Iterator it(vertexFaces(k)); it.More(); it.Next()) {
                const int fi = m_faces.FindIndex(it.Value());
                if(fi > 0) parts.push_back(m_faceNames[fi - 1]);
            }
        }
        m_vertexNames[i - 1] = joinSorted(parts, "<", "&", ">");
    }
    disambiguate(m_vertexNames, m_vertices, "#",
                 [](const TopoDS_Shape &s) { return vertexKey(TopoDS::Vertex(s)); });
    m_vertexByName.clear();
    for(int i = 0; i < nv; ++i) m_vertexByName[m_vertexNames[i]] = i + 1;
}

int NamedShape::count(TopoKind k) const {
    switch(k) {
    case TopoKind::Face: return faceCount();
    case TopoKind::Edge: return edgeCount();
    case TopoKind::Vertex: return vertexCount();
    }
    return 0;
}

TopoDS_Face NamedShape::face(int i) const { return TopoDS::Face(m_faces(i)); }
TopoDS_Edge NamedShape::edge(int i) const { return TopoDS::Edge(m_edges(i)); }
TopoDS_Vertex NamedShape::vertex(int i) const { return TopoDS::Vertex(m_vertices(i)); }

TopoDS_Shape NamedShape::shapeOf(TopoKind k, int i) const {
    switch(k) {
    case TopoKind::Face: return m_faces(i);
    case TopoKind::Edge: return m_edges(i);
    case TopoKind::Vertex: return m_vertices(i);
    }
    return {};
}

const std::string &NamedShape::nameOf(TopoKind k, int i) const {
    if(i < 1 || i > count(k)) return kEmpty;
    switch(k) {
    case TopoKind::Face: return faceName(i);
    case TopoKind::Edge: return edgeName(i);
    case TopoKind::Vertex: return vertexName(i);
    }
    return kEmpty;
}

int NamedShape::indexOf(TopoKind k, const TopoDS_Shape &s) const {
    switch(k) {
    case TopoKind::Face: return m_faces.FindIndex(s);
    case TopoKind::Edge: return m_edges.FindIndex(s);
    case TopoKind::Vertex: return m_vertices.FindIndex(s);
    }
    return 0;
}

int NamedShape::indexOfName(TopoKind k, const std::string &name) const {
    const auto &m = k == TopoKind::Face ? m_faceByName : k == TopoKind::Edge ? m_edgeByName : m_vertexByName;
    auto it = m.find(name);
    return it == m.end() ? 0 : it->second;
}

std::vector<int> NamedShape::indicesWithAlias(const std::string &alias) const {
    std::vector<int> out;
    for(size_t i = 0; i < m_faceAliases.size(); ++i)
        for(const auto &a : m_faceAliases[i])
            if(a == alias) {
                out.push_back(int(i) + 1);
                break;
            }
    return out;
}

std::vector<int> NamedShape::indicesWithBaseName(TopoKind k, const std::string &base) const {
    std::vector<int> out;
    const int n = count(k);
    for(int i = 1; i <= n; ++i)
        if(baseName(nameOf(k, i)) == base) out.push_back(i);
    return out;
}

bool NamedShape::isSelectableEdge(int i) const {
    return i >= 1 && i <= edgeCount() && m_edgeSelectable[i - 1];
}

NamedShape NamedShape::subShape(const TopoDS_Shape &part) const {
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(part, TopAbs_FACE, faces);
    std::vector<FaceLabel> labels(size_t(faces.Extent()));
    for(int i = 1; i <= faces.Extent(); ++i) {
        const int j = m_faces.FindIndex(faces(i));
        if(j > 0) labels[i - 1] = {m_faceNames[j - 1], m_faceAliases[j - 1]};
    }
    return NamedShape(part, std::move(labels));
}

std::vector<FaceLabel> NamedShape::faceLabels() const {
    std::vector<FaceLabel> out;
    for(size_t i = 0; i < m_faceNames.size(); ++i) out.push_back({m_faceNames[i], m_faceAliases[i]});
    return out;
}

// ---------------------------------------------------------------------------

NamedShape propagateNames(const TopoDS_Shape &result, const std::vector<const NamedShape *> &inputs,
                          const Handle(BRepTools_History) & history, const std::string &prefix) {
    TopTools_IndexedMapOfShape resFaces;
    TopExp::MapShapes(result, TopAbs_FACE, resFaces);
    const int n = resFaces.Extent();
    std::vector<std::vector<std::string>> primary(size_t(n) + 1), aliases(size_t(n) + 1), generated(size_t(n) + 1);

    auto imagesOf = [&](const TopoDS_Shape &s, bool modified) -> TopTools_ListOfShape {
        TopTools_ListOfShape out;
        if(history.IsNull()) return out;
        const TopTools_ListOfShape &l = modified ? history->Modified(s) : history->Generated(s);
        for(TopTools_ListOfShape::Iterator it(l); it.More(); it.Next()) out.Append(it.Value());
        return out;
    };

    for(const NamedShape *in : inputs) {
        if(!in) continue;
        for(int i = 1; i <= in->faceCount(); ++i) {
            const TopoDS_Face f = in->face(i);
            auto credit = [&](const TopoDS_Shape &img) {
                const int j = resFaces.FindIndex(img);
                if(j <= 0) return;
                primary[j].push_back(in->faceName(i));
                for(const auto &a : in->faceAliases(i)) aliases[j].push_back(a);
            };
            const int direct = resFaces.FindIndex(f);
            if(direct > 0) {
                credit(f);
            } else if(!history.IsNull() && !history->IsRemoved(f)) {
                for(const auto &img : imagesOf(f, true)) credit(img);
            }
            for(const auto &img : imagesOf(f, false)) {
                const int j = resFaces.FindIndex(img);
                if(j > 0) generated[j].push_back("f:" + in->faceName(i));
            }
        }
        if(history.IsNull() || !history->HasGenerated()) continue;
        for(int i = 1; i <= in->edgeCount(); ++i) {
            for(const auto &img : imagesOf(in->edge(i), false)) {
                const int j = resFaces.FindIndex(img);
                if(j > 0) generated[j].push_back(in->edgeName(i));
            }
        }
        for(int i = 1; i <= in->vertexCount(); ++i) {
            for(const auto &img : imagesOf(in->vertex(i), false)) {
                const int j = resFaces.FindIndex(img);
                if(j > 0) generated[j].push_back("v:" + in->vertexName(i));
            }
        }
    }

    std::vector<FaceLabel> labels(static_cast<size_t>(n));
    for(int j = 1; j <= n; ++j) {
        FaceLabel &lab = labels[j - 1];
        auto &names = primary[j];
        if(!names.empty()) {
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            lab.name = names.front();
            std::set<std::string> al(aliases[j].begin(), aliases[j].end());
            for(size_t k = 1; k < names.size(); ++k) al.insert(names[k]);
            al.erase(lab.name);
            lab.aliases.assign(al.begin(), al.end());
        } else if(!generated[j].empty()) {
            auto &g = generated[j];
            std::sort(g.begin(), g.end());
            lab.name = prefix + "/" + g.front();
        } else {
            lab.name = prefix + "/new";
        }
    }
    return NamedShape(result, std::move(labels));
}

} // namespace cad
