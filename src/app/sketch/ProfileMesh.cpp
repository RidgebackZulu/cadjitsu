#include "sketch/ProfileMesh.h"

#include "sketch/ProfileToFace.h"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>

#include <algorithm>
#include <cmath>

namespace cadly {

std::vector<QVector3D> triangulateProfile(const cad::Profile &profile, const gp_Ax3 &frame) {
    std::vector<QVector3D> out;
    cad::ProfileFace pf;
    std::string error;
    if(!cad::profileToFace(profile, frame, pf, error)) return out;
    const double size = std::sqrt(std::max(std::fabs(profile.area), 1e-6));
    BRepMesh_IncrementalMesh mesher(pf.face, std::clamp(size * 0.004, 0.002, 0.5), false, 0.3, false);
    TopLoc_Location loc;
    const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(pf.face, loc);
    if(tri.IsNull()) return out;
    const gp_Trsf tr = loc.Transformation();
    out.reserve(size_t(tri->NbTriangles()) * 3);
    for(int i = 1; i <= tri->NbTriangles(); ++i) {
        int n[3];
        tri->Triangle(i).Get(n[0], n[1], n[2]);
        for(int k : n) {
            const gp_Pnt p = tri->Node(k).Transformed(tr);
            out.emplace_back(float(p.X()), float(p.Y()), float(p.Z()));
        }
    }
    return out;
}

} // namespace cadly
