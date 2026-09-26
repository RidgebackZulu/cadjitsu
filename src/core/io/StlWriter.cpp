#include "io/StlWriter.h"

#include "base/KernelLock.h"
#include "geom/OcctUtil.h"

#include <BRepAlgoAPI_Fuse.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopTools_ListOfShape.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace cad {

void stlTolerances(const StlOptions &o, double &deflection, double &angleDegrees) {
    switch(o.resolution) {
    case StlResolution::Coarse:
        deflection = 0.1;
        angleDegrees = 30.0;
        break;
    case StlResolution::Medium:
        deflection = 0.02;
        angleDegrees = 15.0;
        break;
    case StlResolution::Fine:
        deflection = 0.005;
        angleDegrees = 7.5;
        break;
    case StlResolution::Custom:
        deflection = std::max(1e-4, o.deflection);
        angleDegrees = std::clamp(o.angleDegrees, 1.0, 60.0);
        break;
    }
}

bool buildStlMesh(const std::vector<TopoDS_Shape> &solids, const StlOptions &options, StlExport &out,
                  std::string &error) {
    const KernelLock kernel(kernelMutex());
    out = StlExport();
    if(solids.empty()) {
        error = "there are no bodies to export";
        return false;
    }
    TopoDS_Shape shape;
    if(solids.size() == 1) {
        shape = solids[0];
    } else if(options.mergeBodies) {
        TopTools_ListOfShape args, tools;
        args.Append(solids[0]);
        for(size_t i = 1; i < solids.size(); ++i) tools.Append(solids[i]);
        BRepAlgoAPI_Fuse fuse;
        fuse.SetArguments(args);
        fuse.SetTools(tools);
        fuse.SetNonDestructive(Standard_True);
        fuse.Build();
        if(!fuse.IsDone() || fuse.HasErrors()) {
            error = "could not merge the bodies";
            return false;
        }
        fuse.SimplifyResult();
        shape = fuse.Shape();
    } else {
        std::vector<TopoDS_Shape> v(solids.begin(), solids.end());
        shape = makeCompound(v);
    }

    double deflection, angle;
    stlTolerances(options, deflection, angle);
    if(!weldedMesh(shape, deflection, angle * M_PI / 180.0, out.mesh, error)) return false;

    out.solidVolume = volumeOf(shape);
    const double tol = std::max(areaOf(shape) * deflection * 1.5, 1e-6 * out.solidVolume);
    out.report = validateMesh(out.mesh, out.solidVolume, tol);
    if(!out.report.watertight) {
        // Fallback: weld coincident vertices by distance and re-check.
        weldByDistance(out.mesh, std::max(deflection * 1e-3, 1e-6));
        out.report = validateMesh(out.mesh, out.solidVolume, tol);
    }
    return true;
}

bool writeStlFile(const std::string &path, const TriMesh &mesh, bool binary, const std::string &name,
                  std::string &error) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if(!f) {
        error = "cannot write " + path;
        return false;
    }
    auto normalOf = [&](const std::array<uint32_t, 3> &t, float n[3]) {
        const auto &a = mesh.vertices[t[0]], &b = mesh.vertices[t[1]], &c = mesh.vertices[t[2]];
        const double ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const double vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
        if(l > 0) {
            nx /= l;
            ny /= l;
            nz /= l;
        }
        n[0] = float(nx);
        n[1] = float(ny);
        n[2] = float(nz);
    };
    if(binary) {
        char header[80] = {};
        std::snprintf(header, sizeof header, "Cadly STL %s", name.c_str());
        f.write(header, 80);
        const uint32_t count = uint32_t(mesh.triangles.size());
        f.write(reinterpret_cast<const char *>(&count), 4);
        for(const auto &t : mesh.triangles) {
            float rec[12];
            normalOf(t, rec);
            for(int k = 0; k < 3; ++k)
                for(int c = 0; c < 3; ++c) rec[3 + 3 * k + c] = float(mesh.vertices[t[k]][c]);
            f.write(reinterpret_cast<const char *>(rec), sizeof rec);
            const uint16_t attr = 0;
            f.write(reinterpret_cast<const char *>(&attr), 2);
        }
    } else {
        f << "solid " << name << "\n";
        char buf[256];
        for(const auto &t : mesh.triangles) {
            float n[3];
            normalOf(t, n);
            std::snprintf(buf, sizeof buf, "  facet normal %g %g %g\n    outer loop\n", n[0], n[1], n[2]);
            f << buf;
            for(int k = 0; k < 3; ++k) {
                const auto &v = mesh.vertices[t[k]];
                std::snprintf(buf, sizeof buf, "      vertex %.9g %.9g %.9g\n", v[0], v[1], v[2]);
                f << buf;
            }
            f << "    endloop\n  endfacet\n";
        }
        f << "endsolid " << name << "\n";
    }
    if(!f) {
        error = "failed writing " + path;
        return false;
    }
    return true;
}

bool readStlFile(const std::string &path, TriMesh &mesh, std::string &error) {
    mesh = TriMesh();
    std::ifstream f(path, std::ios::binary);
    if(!f) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string data = ss.str();
    // Vertices are deduplicated exactly so topology can be validated.
    std::map<std::array<float, 3>, uint32_t> index;
    auto vid = [&](float x, float y, float z) {
        const std::array<float, 3> key{x, y, z};
        auto it = index.find(key);
        if(it != index.end()) return it->second;
        const uint32_t id = uint32_t(mesh.vertices.size());
        mesh.vertices.push_back({x, y, z});
        index.emplace(key, id);
        return id;
    };
    const bool ascii = data.rfind("solid", 0) == 0 && data.find("facet") != std::string::npos;
    if(!ascii) {
        if(data.size() < 84) {
            error = "truncated STL";
            return false;
        }
        uint32_t count;
        std::memcpy(&count, data.data() + 80, 4);
        if(data.size() < 84 + size_t(count) * 50) {
            error = "truncated STL";
            return false;
        }
        for(uint32_t i = 0; i < count; ++i) {
            float rec[12];
            std::memcpy(rec, data.data() + 84 + size_t(i) * 50, sizeof rec);
            mesh.triangles.push_back({vid(rec[3], rec[4], rec[5]), vid(rec[6], rec[7], rec[8]), vid(rec[9], rec[10], rec[11])});
        }
    } else {
        std::istringstream in(data);
        std::string word;
        std::vector<uint32_t> cur;
        while(in >> word) {
            if(word == "vertex") {
                float x, y, z;
                in >> x >> y >> z;
                cur.push_back(vid(x, y, z));
                if(cur.size() == 3) {
                    mesh.triangles.push_back({cur[0], cur[1], cur[2]});
                    cur.clear();
                }
            }
        }
    }
    return true;
}

} // namespace cad
