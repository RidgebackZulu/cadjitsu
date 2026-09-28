#include "sketch/ProfileToFace.h"

#include "geom/OcctUtil.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <ShapeFix_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <map>

namespace cad {

namespace {

struct Placement {
    gp_Ax3 frame;
    gp_Pnt at(Vec2 p) const {
        return gp_Pnt(frame.Location().XYZ() + frame.XDirection().XYZ() * p.x + frame.YDirection().XYZ() * p.y);
    }
    Vec2 back(const gp_Pnt &p) const {
        const gp_XYZ d = p.XYZ() - frame.Location().XYZ();
        return {d.Dot(frame.XDirection().XYZ()), d.Dot(frame.YDirection().XYZ())};
    }
};

// Merges consecutive segments that belong to the same sketch curve.
std::vector<ProfileSeg> mergeSegments(const std::vector<ProfileSeg> &segs, bool &fullCircle) {
    fullCircle = false;
    if(segs.empty()) return segs;
    bool allSame = true;
    for(const auto &s : segs)
        if(s.curveId != segs[0].curveId || s.isArc != segs[0].isArc) allSame = false;
    if(allSame && segs[0].isArc && segs[0].curveId != 0) {
        fullCircle = true;
        ProfileSeg one = segs[0];
        for(const auto &s : segs) one.key = std::min(one.key, s.key);
        return {one};
    }
    // Rotate so the loop starts at a curve boundary.
    const size_t n = segs.size();
    size_t start = 0;
    for(size_t i = 0; i < n; ++i) {
        const auto &prev = segs[(i + n - 1) % n];
        if(prev.curveId != segs[i].curveId || prev.isArc != segs[i].isArc) {
            start = i;
            break;
        }
    }
    std::vector<ProfileSeg> out;
    for(size_t k = 0; k < n; ++k) {
        const ProfileSeg &s = segs[(start + k) % n];
        // (Pieces of no sketch curve, id 0 - text outlines - stay apart.)
        if(!out.empty() && s.curveId != 0 && out.back().curveId == s.curveId && out.back().isArc == s.isArc &&
           out.back().ccw == s.ccw) {
            out.back().p1 = s.p1; // extend
            out.back().key = std::min(out.back().key, s.key);
            continue;
        }
        out.push_back(s);
    }
    return out;
}

bool buildWire(const ProfileLoop &loop, const Placement &pl, std::map<std::pair<double, double>, TopoDS_Vertex> &verts,
               TopoDS_Wire &wire, std::vector<std::pair<Vec2, std::string>> &mids, std::string &error) {
    bool fullCircle = false;
    const std::vector<ProfileSeg> segs = mergeSegments(loop.segs, fullCircle);
    const gp_Dir normal = pl.frame.Direction();
    BRepBuilderAPI_MakeWire mw;

    auto vertexAt = [&](Vec2 p) {
        auto key = std::make_pair(p.x, p.y);
        auto it = verts.find(key);
        if(it != verts.end()) return it->second;
        TopoDS_Vertex v = BRepBuilderAPI_MakeVertex(pl.at(p)).Vertex();
        verts.emplace(key, v);
        return v;
    };

    if(fullCircle) {
        const ProfileSeg &s = segs[0];
        gp_Ax2 ax(pl.at(s.c), normal, pl.frame.XDirection());
        gp_Circ circ(ax, s.r);
        BRepBuilderAPI_MakeEdge me(circ);
        if(!me.IsDone()) {
            error = "could not build a circle edge";
            return false;
        }
        TopoDS_Edge e = me.Edge();
        if(!s.ccw) e.Reverse();
        mw.Add(e);
        // The edge's parametric midpoint (angle pi from the frame X axis).
        mids.push_back({s.c - Vec2(s.r, 0), s.key});
    } else {
        for(const auto &s : segs) {
            const TopoDS_Vertex v0 = vertexAt(s.p0), v1 = vertexAt(s.p1);
            TopoDS_Edge e;
            if(!s.isArc) {
                BRepBuilderAPI_MakeEdge me(v0, v1);
                if(!me.IsDone()) {
                    error = "could not build a line edge";
                    return false;
                }
                e = me.Edge();
            } else {
                gp_Ax2 ax(pl.at(s.c), normal, pl.frame.XDirection());
                gp_Circ circ(ax, s.r);
                // Edges follow the circle's CCW parametrization; CW pieces are reversed.
                BRepBuilderAPI_MakeEdge me(circ, s.ccw ? v0 : v1, s.ccw ? v1 : v0);
                if(!me.IsDone()) {
                    error = "could not build an arc edge";
                    return false;
                }
                e = me.Edge();
                if(!s.ccw) e.Reverse();
            }
            mw.Add(e);
            if(!mw.IsDone()) {
                error = "profile edges do not connect";
                return false;
            }
            mids.push_back({s.midpoint(), s.key});
        }
    }
    if(!mw.IsDone()) {
        error = "could not build the profile wire";
        return false;
    }
    wire = mw.Wire();
    return true;
}

} // namespace

bool profileToFace(const Profile &profile, const gp_Ax3 &frame, ProfileFace &out, std::string &error) {
    out = ProfileFace();
    const Placement pl{frame};
    std::map<std::pair<double, double>, TopoDS_Vertex> verts;
    std::vector<std::pair<Vec2, std::string>> mids;

    TopoDS_Wire outer;
    if(!buildWire(profile.outer, pl, verts, outer, mids, error)) return false;
    BRepBuilderAPI_MakeFace mf(gp_Pln(frame), outer, Standard_True);
    if(!mf.IsDone()) {
        error = "could not build the profile face";
        return false;
    }
    for(const auto &hole : profile.holes) {
        TopoDS_Wire w;
        if(!buildWire(hole, pl, verts, w, mids, error)) return false;
        mf.Add(w);
    }
    TopoDS_Face face = mf.Face();
    if(!BRepCheck_Analyzer(face).IsValid()) {
        ShapeFix_Face fix(face);
        fix.Perform();
        face = fix.Face();
        if(!BRepCheck_Analyzer(face).IsValid()) {
            error = "the profile face is invalid";
            return false;
        }
    }
    out.face = face;

    // Key every final edge by its nearest source segment midpoint.
    for(TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
        const TopoDS_Edge e = TopoDS::Edge(ex.Current());
        const Vec2 m = pl.back(midpointOfEdge(e));
        std::string best;
        double bestD = 1e300;
        for(const auto &[pt, key] : mids) {
            const double d = distance(pt, m);
            if(d < bestD) {
                bestD = d;
                best = key;
            }
        }
        out.edgeKeys.push_back({e, best});
    }
    return true;
}

} // namespace cad
