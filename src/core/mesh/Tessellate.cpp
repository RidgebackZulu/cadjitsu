#include "mesh/MeshData.h"

#include "topo/NamedShape.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace cad {

double defaultDeflection(double modelSize) { return std::clamp(modelSize * 0.0008, 0.005, 0.5); }

namespace {

bool hasTriangulation(const TopoDS_Face &face) {
    TopLoc_Location loc;
    return !BRep_Tool::Triangulation(face, loc).IsNull();
}

TopoDS_Shape meshedCopy(const TopoDS_Shape &shape, double deflection, double angle, int *status = nullptr) {
    BRepBuilderAPI_Copy copier(shape, Standard_True, Standard_False);
    TopoDS_Shape copy = copier.Shape();
    BRepMesh_IncrementalMesh mesher(copy, deflection, Standard_False, angle, Standard_True);
    if(status) *status = mesher.GetStatusFlags();
    // A face the mesher gave up on (it happens with tight tolerances on some
    // platforms) is meshed again on its own, on this thread, and then with
    // looser tolerances, before the caller reports it.
    for(TopExp_Explorer ex(copy, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        for(double scale : {1.0, 4.0, 20.0}) {
            if(hasTriangulation(face)) break;
            BRepMesh_IncrementalMesh again(face, deflection * scale, Standard_False, std::min(angle * scale, 0.5),
                                           Standard_False);
        }
    }
    return copy;
}

std::string describeFace(const TopoDS_Face &face) {
    BRepAdaptor_Surface surf(face);
    static const char *names[] = {"plane", "cylinder", "cone", "sphere", "torus", "bezier", "bspline",
                                  "revolution", "extrusion", "offset", "other"};
    const int t = std::clamp(int(surf.GetType()), 0, 10);
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s, tolerance %.2g, u %.4g..%.4g, v %.4g..%.4g", names[t],
                  BRep_Tool::Tolerance(face), surf.FirstUParameter(), surf.LastUParameter(),
                  surf.FirstVParameter(), surf.LastVParameter());
    return buf;
}

} // namespace

std::shared_ptr<MeshData> tessellateForDisplay(const NamedShape &named, double deflection, double angle) {
    auto mesh = std::make_shared<MeshData>();
    if(named.isNull()) return mesh;
    const TopoDS_Shape copy = meshedCopy(named.shape(), deflection, angle);

    TopTools_IndexedMapOfShape faces, edges;
    TopExp::MapShapes(copy, TopAbs_FACE, faces);
    TopExp::MapShapes(copy, TopAbs_EDGE, edges);
    mesh->faceRanges.resize(size_t(faces.Extent()));
    mesh->edgeRanges.resize(size_t(edges.Extent()));

    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};

    for(int fi = 1; fi <= faces.Extent(); ++fi) {
        const TopoDS_Face face = TopoDS::Face(faces(fi));
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        MeshData::Range &range = mesh->faceRanges[fi - 1];
        range.first = uint32_t(mesh->indices.size());
        if(tri.IsNull()) continue;
        if(!tri->HasNormals()) BRepLib_ToolTriangulatedShape::ComputeNormals(face, tri);
        const gp_Trsf trsf = loc.Transformation();
        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        const uint32_t base = uint32_t(mesh->vertexCount());
        for(int i = 1; i <= tri->NbNodes(); ++i) {
            const gp_Pnt p = tri->Node(i).Transformed(trsf);
            gp_Dir n = tri->HasNormals() ? tri->Normal(i) : gp_Dir(0, 0, 1);
            n.Transform(trsf);
            if(reversed) n.Reverse();
            const float xyz[3] = {float(p.X()), float(p.Y()), float(p.Z())};
            for(int k = 0; k < 3; ++k) {
                mesh->positions.push_back(xyz[k]);
                lo[k] = std::min(lo[k], xyz[k]);
                hi[k] = std::max(hi[k], xyz[k]);
            }
            mesh->normals.push_back(float(n.X()));
            mesh->normals.push_back(float(n.Y()));
            mesh->normals.push_back(float(n.Z()));
        }
        for(int t = 1; t <= tri->NbTriangles(); ++t) {
            int a, b, c;
            tri->Triangle(t).Get(a, b, c);
            if(reversed) std::swap(b, c);
            mesh->indices.push_back(base + uint32_t(a - 1));
            mesh->indices.push_back(base + uint32_t(b - 1));
            mesh->indices.push_back(base + uint32_t(c - 1));
            mesh->triangleFace.push_back(uint32_t(fi));
        }
        range.count = uint32_t(mesh->indices.size()) - range.first;
    }

    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(copy, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    for(int ei = 1; ei <= edges.Extent(); ++ei) {
        const TopoDS_Edge edge = TopoDS::Edge(edges(ei));
        MeshData::Range &range = mesh->edgeRanges[ei - 1];
        range.first = uint32_t(mesh->edgePoints.size() / 3);
        if(BRep_Tool::Degenerated(edge)) continue;
        const int k = edgeFaces.FindIndex(edge);
        if(k <= 0 || edgeFaces(k).IsEmpty()) continue;
        const TopoDS_Face face = TopoDS::Face(edgeFaces(k).First());
        // Seams of periodic faces (the "cut" line of a cylinder) are not real edges.
        if(BRep_Tool::IsClosed(edge, face)) continue;
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if(tri.IsNull()) continue;
        Handle(Poly_PolygonOnTriangulation) poly = BRep_Tool::PolygonOnTriangulation(edge, tri, loc);
        if(poly.IsNull()) continue;
        const gp_Trsf trsf = loc.Transformation();
        const TColStd_Array1OfInteger &nodes = poly->Nodes();
        for(int i = nodes.Lower(); i <= nodes.Upper(); ++i) {
            const gp_Pnt p = tri->Node(nodes(i)).Transformed(trsf);
            mesh->edgePoints.push_back(float(p.X()));
            mesh->edgePoints.push_back(float(p.Y()));
            mesh->edgePoints.push_back(float(p.Z()));
        }
        range.count = uint32_t(mesh->edgePoints.size() / 3) - range.first;
    }

    TopTools_IndexedMapOfShape verts;
    TopExp::MapShapes(copy, TopAbs_VERTEX, verts);
    for(int vi = 1; vi <= verts.Extent(); ++vi) {
        const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(verts(vi)));
        mesh->vertexPoints.push_back(float(p.X()));
        mesh->vertexPoints.push_back(float(p.Y()));
        mesh->vertexPoints.push_back(float(p.Z()));
    }

    if(mesh->vertexCount() > 0) {
        for(int k = 0; k < 3; ++k) {
            mesh->bboxMin[k] = lo[k];
            mesh->bboxMax[k] = hi[k];
        }
    }
    return mesh;
}

bool weldedMesh(const TopoDS_Shape &shape, double deflection, double angle, TriMesh &out, std::string &error) {
    out = TriMesh();
    if(shape.IsNull()) {
        error = "nothing to mesh";
        return false;
    }
    int status = 0;
    const TopoDS_Shape copy = meshedCopy(shape, deflection, angle, &status);

    TopTools_IndexedMapOfShape vertices, edges, faces;
    TopExp::MapShapes(copy, TopAbs_VERTEX, vertices);
    TopExp::MapShapes(copy, TopAbs_EDGE, edges);
    TopExp::MapShapes(copy, TopAbs_FACE, faces);

    auto addVertex = [&](const gp_Pnt &p) {
        out.vertices.push_back({p.X(), p.Y(), p.Z()});
        return uint32_t(out.vertices.size() - 1);
    };
    std::vector<uint32_t> vertexId(size_t(vertices.Extent()) + 1);
    for(int i = 1; i <= vertices.Extent(); ++i) vertexId[i] = addVertex(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))));

    // Global ids of the discretization points of each edge, created once and
    // shared by every face bounded by that edge.
    std::vector<std::vector<uint32_t>> edgeIds(size_t(edges.Extent()) + 1);
    std::vector<std::vector<gp_Pnt>> edgePts(size_t(edges.Extent()) + 1);

    auto dist2 = [&](uint32_t id, const gp_Pnt &p) {
        const auto &v = out.vertices[id];
        const double dx = v[0] - p.X(), dy = v[1] - p.Y(), dz = v[2] - p.Z();
        return dx * dx + dy * dy + dz * dz;
    };

    for(int fi = 1; fi <= faces.Extent(); ++fi) {
        const TopoDS_Face face = TopoDS::Face(faces(fi));
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if(tri.IsNull()) {
            error = "a face could not be meshed (face " + std::to_string(fi) + " of " + std::to_string(faces.Extent()) +
                    ": " + describeFace(face) + "; mesher status " + std::to_string(status) + ")";
            return false;
        }
        const gp_Trsf trsf = loc.Transformation();
        std::vector<int64_t> nodeId(size_t(tri->NbNodes()) + 1, -1);

        for(TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(ex.Current());
            Handle(Poly_PolygonOnTriangulation) poly = BRep_Tool::PolygonOnTriangulation(edge, tri, loc);
            if(poly.IsNull()) continue;
            const TColStd_Array1OfInteger &nodes = poly->Nodes();
            const int n = nodes.Length();
            const int ei = edges.FindIndex(edge);
            TopoDS_Vertex v1, v2;
            TopExp::Vertices(edge, v1, v2);
            if(BRep_Tool::Degenerated(edge)) {
                const uint32_t id = vertexId[vertices.FindIndex(v1)];
                for(int k = nodes.Lower(); k <= nodes.Upper(); ++k) nodeId[nodes(k)] = id;
                continue;
            }
            std::vector<gp_Pnt> pts;
            for(int k = nodes.Lower(); k <= nodes.Upper(); ++k) pts.push_back(tri->Node(nodes(k)).Transformed(trsf));
            auto &ids = edgeIds[ei];
            if(ids.empty()) {
                ids.resize(size_t(n));
                const uint32_t i1 = vertexId[vertices.FindIndex(v1)];
                const uint32_t i2 = vertexId[vertices.FindIndex(v2)];
                // Orient the shared list from v1 (closest) to v2.
                const bool forward = dist2(i1, pts.front()) <= dist2(i1, pts.back());
                ids.front() = forward ? i1 : i2;
                ids.back() = forward ? i2 : i1;
                if(i1 == i2) ids.front() = ids.back() = i1; // closed edge (full circle)
                for(int k = 1; k < n - 1; ++k) ids[k] = addVertex(pts[k]);
                edgePts[ei] = pts;
            }
            if(int(ids.size()) != n) {
                error = "inconsistent edge discretization";
                return false;
            }
            // Same direction as the stored list, or reversed? Compare the
            // second point too, which also works for closed edges (circles).
            const auto &ref = edgePts[ei];
            bool same;
            if(n > 2) {
                same = ref[1].SquareDistance(pts[1]) <= ref[1].SquareDistance(pts[n - 2]);
            } else {
                same = ref.front().SquareDistance(pts.front()) + ref.back().SquareDistance(pts.back()) <=
                       ref.front().SquareDistance(pts.back()) + ref.back().SquareDistance(pts.front());
            }
            for(int k = 0; k < n; ++k) nodeId[nodes(nodes.Lower() + k)] = same ? ids[k] : ids[n - 1 - k];
        }

        for(int i = 1; i <= tri->NbNodes(); ++i)
            if(nodeId[i] < 0) nodeId[i] = addVertex(tri->Node(i).Transformed(trsf));

        const bool reversed = face.Orientation() == TopAbs_REVERSED;
        for(int t = 1; t <= tri->NbTriangles(); ++t) {
            int a, b, c;
            tri->Triangle(t).Get(a, b, c);
            if(reversed) std::swap(b, c);
            const uint32_t ia = uint32_t(nodeId[a]), ib = uint32_t(nodeId[b]), ic = uint32_t(nodeId[c]);
            if(ia == ib || ib == ic || ia == ic) continue;
            out.triangles.push_back({ia, ib, ic});
        }
    }
    return true;
}

void weldByDistance(TriMesh &mesh, double tolerance) {
    const double cell = std::max(tolerance * 2.0, 1e-12);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    std::vector<uint32_t> remap(mesh.vertices.size());
    std::vector<std::array<double, 3>> kept;
    auto key = [&](int64_t x, int64_t y, int64_t z) {
        return uint64_t(x) * 73856093ull ^ uint64_t(y) * 19349663ull ^ uint64_t(z) * 83492791ull;
    };
    for(size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto &v = mesh.vertices[i];
        const int64_t cx = int64_t(std::floor(v[0] / cell)), cy = int64_t(std::floor(v[1] / cell)),
                      cz = int64_t(std::floor(v[2] / cell));
        int64_t found = -1;
        for(int64_t dx = -1; dx <= 1 && found < 0; ++dx)
            for(int64_t dy = -1; dy <= 1 && found < 0; ++dy)
                for(int64_t dz = -1; dz <= 1 && found < 0; ++dz) {
                    auto it = grid.find(key(cx + dx, cy + dy, cz + dz));
                    if(it == grid.end()) continue;
                    for(uint32_t j : it->second) {
                        const auto &w = kept[j];
                        const double d = std::hypot(w[0] - v[0], w[1] - v[1], w[2] - v[2]);
                        if(d <= tolerance) {
                            found = j;
                            break;
                        }
                    }
                }
        if(found < 0) {
            found = int64_t(kept.size());
            kept.push_back(v);
            grid[key(cx, cy, cz)].push_back(uint32_t(found));
        }
        remap[i] = uint32_t(found);
    }
    std::vector<std::array<uint32_t, 3>> tris;
    for(auto t : mesh.triangles) {
        for(auto &i : t) i = remap[i];
        if(t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
        tris.push_back(t);
    }
    mesh.vertices = std::move(kept);
    mesh.triangles = std::move(tris);
}

} // namespace cad
