#include "features/ConstructionPlaneFeature.h"

#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <gp_Ax1.hxx>

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

const char *axisName(PlaneRotationAxis a) {
    switch(a) {
    case PlaneRotationAxis::LocalX: return "localX";
    case PlaneRotationAxis::LocalY: return "localY";
    case PlaneRotationAxis::Edge: return "edge";
    }
    return "localX";
}

PlaneRotationAxis axisFromName(const std::string &s) {
    if(s == "localY") return PlaneRotationAxis::LocalY;
    if(s == "edge") return PlaneRotationAxis::Edge;
    return PlaneRotationAxis::LocalX;
}

} // namespace

std::vector<ParamDef> ConstructionPlaneFeature::params() const {
    std::vector<ParamDef> out;
    if(!offset.empty()) out.push_back({offset.name, offset.expr, ValueKind::Length, id, name + " Offset"});
    if(!angle.empty()) out.push_back({angle.name, angle.expr, ValueKind::Angle, id, name + " Angle"});
    return out;
}

json ConstructionPlaneFeature::dataToJson() const {
    json j{{"base", base.toJson()}, {"offset", offset.toJson()}, {"angle", angle.toJson()}, {"axis", axisName(axis)}};
    if(!axisEdge.empty()) j["axisEdge"] = axisEdge.toJson();
    return j;
}

void ConstructionPlaneFeature::dataFromJson(const json &j) {
    base = PlaneRef::fromJson(j.value("base", json()));
    offset = ParamSlot::fromJson(j.value("offset", json()));
    angle = ParamSlot::fromJson(j.value("angle", json()));
    axis = axisFromName(jget<std::string>(j, "axis", ""));
    axisEdge = TopoRef::fromJson(j.value("axisEdge", json()));
}

std::vector<FeatureId> ConstructionPlaneFeature::dependencies() const {
    if(base.kind == PlaneRef::Kind::Construction) return {base.plane};
    return {};
}

FeatureResult ConstructionPlaneFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        gp_Ax3 frame;
        if(!resolvePlane(*input, base, frame, st)) return {input, st};

        // Drawn around the base: a face's middle, a construction plane's own
        // centre, or the origin.
        gp_Pnt centre = frame.Location();
        if(base.kind == PlaneRef::Kind::Face) {
            const ResolvedRef r = resolveRef(*input, base.face);
            if(r.ok) {
                GProp_GProps g;
                BRepGProp::SurfaceProperties(r.shape, g);
                const gp_Vec off(frame.Location(), g.CentreOfMass());
                centre = g.CentreOfMass().Translated(-gp_Vec(frame.Direction()) * off.Dot(gp_Vec(frame.Direction())));
            }
        } else if(base.kind == PlaneRef::Kind::Construction) {
            if(auto it = input->planes.find(base.plane); it != input->planes.end()) centre = it->second->center;
        }

        double d = 0.0, a = 0.0;
        if(!offset.empty() && !ctx.value(offset, d, st)) return {input, st};
        if(!angle.empty() && !ctx.value(angle, a, st, ValueKind::Angle)) return {input, st};
        frame.Translate(gp_Vec(frame.Direction()) * d);
        centre.Translate(gp_Vec(frame.Direction()) * d);

        if(std::fabs(a) > 1e-12) {
            gp_Ax1 ax;
            switch(axis) {
            case PlaneRotationAxis::LocalX: ax = gp_Ax1(frame.Location(), frame.XDirection()); break;
            case PlaneRotationAxis::LocalY: ax = gp_Ax1(frame.Location(), frame.YDirection()); break;
            case PlaneRotationAxis::Edge: {
                ResolvedRef r = resolveRef(*input, axisEdge);
                st.merge(r.status);
                if(!r.ok) return {input, st};
                BRepAdaptor_Curve c(TopoDS::Edge(r.shape));
                if(c.GetType() != GeomAbs_Line) return {input, Status::error("the rotation axis must be a straight edge")};
                ax = gp_Ax1(c.Line().Location(), c.Line().Direction());
                break;
            }
            }
            frame.Rotate(ax, a);
            centre.Rotate(ax, a);
        }

        auto plane = std::make_shared<PlaneResult>();
        plane->feature = id;
        plane->name = name;
        plane->frame = frame;
        plane->center = centre;
        plane->halfSize = std::max(20.0, input->modelSize() * 0.6);
        auto out = std::make_shared<ModelState>(*input);
        out->planes[id] = plane;
        return {out, st};
    });
}

} // namespace cad
