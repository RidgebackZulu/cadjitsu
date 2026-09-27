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

gp_Ax3 tiltedFrame(const gp_Ax3 &frame, const gp_Pnt &pivot, double ax, double ay) {
    gp_Ax3 f = frame;
    if(std::fabs(ax) > 1e-12) f.Rotate(gp_Ax1(pivot, f.XDirection()), ax);
    if(std::fabs(ay) > 1e-12) f.Rotate(gp_Ax1(pivot, f.YDirection()), ay);
    return f;
}

std::vector<ParamDef> ConstructionPlaneFeature::params() const {
    std::vector<ParamDef> out;
    if(!offset.empty()) out.push_back({offset.name, offset.expr, ValueKind::Length, id, name + " Offset"});
    const bool twoAxis = axis == PlaneRotationAxis::LocalX;
    if(!angle.empty())
        out.push_back({angle.name, angle.expr, ValueKind::Angle, id,
                       name + (twoAxis ? " Tilt X" : axis == PlaneRotationAxis::LocalY ? " Tilt Y" : " Angle")});
    if(!angleY.empty() && twoAxis) out.push_back({angleY.name, angleY.expr, ValueKind::Angle, id, name + " Tilt Y"});
    return out;
}

json ConstructionPlaneFeature::dataToJson() const {
    json j{{"base", base.toJson()}, {"offset", offset.toJson()}, {"angle", angle.toJson()}, {"axis", axisName(axis)}};
    if(!angleY.empty()) j["angleY"] = angleY.toJson();
    if(pivotAtCenter) j["pivot"] = "center";
    if(!axisEdge.empty()) j["axisEdge"] = axisEdge.toJson();
    return j;
}

void ConstructionPlaneFeature::dataFromJson(const json &j) {
    base = PlaneRef::fromJson(j.value("base", json()));
    offset = ParamSlot::fromJson(j.value("offset", json()));
    angle = ParamSlot::fromJson(j.value("angle", json()));
    angleY = ParamSlot::fromJson(j.value("angleY", json()));
    axis = axisFromName(jget<std::string>(j, "axis", ""));
    axisEdge = TopoRef::fromJson(j.value("axisEdge", json()));
    // Saved before two-axis tilts: keep turning about the frame origin.
    pivotAtCenter = jget<std::string>(j, "pivot", "") == "center";
}

bool ConstructionPlaneFeature::baseFrame(const ModelState &input, gp_Ax3 &frame, gp_Pnt &centre, Status &st) const {
    if(!resolvePlane(input, base, frame, st)) return false;
    // Drawn around the base: a face's middle, a construction plane's own
    // centre, or the origin.
    centre = frame.Location();
    if(base.kind == PlaneRef::Kind::Face) {
        const ResolvedRef r = resolveRef(input, base.face);
        if(r.ok) {
            GProp_GProps g;
            BRepGProp::SurfaceProperties(r.shape, g);
            const gp_Vec off(frame.Location(), g.CentreOfMass());
            centre = g.CentreOfMass().Translated(-gp_Vec(frame.Direction()) * off.Dot(gp_Vec(frame.Direction())));
        }
    } else if(base.kind == PlaneRef::Kind::Construction) {
        if(auto it = input.planes.find(base.plane); it != input.planes.end()) {
            // The base plane's centre, projected into it.
            const gp_Pnt c = it->second->center;
            const gp_Vec off(frame.Location(), c);
            centre = c.Translated(-gp_Vec(frame.Direction()) * off.Dot(gp_Vec(frame.Direction())));
        }
    }
    return true;
}

std::vector<FeatureId> ConstructionPlaneFeature::dependencies() const {
    if(base.kind == PlaneRef::Kind::Construction) return {base.plane};
    return {};
}

FeatureResult ConstructionPlaneFeature::compute(const StatePtr &input, const ComputeContext &ctx) const {
    return guardedCompute(input, [&]() -> FeatureResult {
        Status st;
        gp_Ax3 frame;
        gp_Pnt centre;
        if(!baseFrame(*input, frame, centre, st)) return {input, st};

        double d = 0.0, a = 0.0, ay = 0.0;
        if(!offset.empty() && !ctx.value(offset, d, st)) return {input, st};
        if(!angle.empty() && !ctx.value(angle, a, st, ValueKind::Angle)) return {input, st};
        if(axis == PlaneRotationAxis::LocalX && !angleY.empty() && !ctx.value(angleY, ay, st, ValueKind::Angle))
            return {input, st};
        frame.Translate(gp_Vec(frame.Direction()) * d);
        centre.Translate(gp_Vec(frame.Direction()) * d);

        // Planes saved before two-axis tilts turn about the frame origin.
        const gp_Pnt pivot = pivotAtCenter ? centre : frame.Location();
        switch(axis) {
        case PlaneRotationAxis::LocalX:
        {
            const gp_Ax3 tilted = tiltedFrame(frame, pivot, a, ay);
            if(!pivotAtCenter) {
                // The centre turns with the plane, about the same axes.
                centre.Rotate(gp_Ax1(pivot, frame.XDirection()), a);
                centre.Rotate(gp_Ax1(pivot, tilted.YDirection()), ay);
            }
            frame = tilted;
            break;
        }
        case PlaneRotationAxis::LocalY:
            if(std::fabs(a) > 1e-12) {
                const gp_Ax1 ax(pivot, frame.YDirection());
                frame.Rotate(ax, a);
                centre.Rotate(ax, a);
            }
            break;
        case PlaneRotationAxis::Edge:
            if(std::fabs(a) > 1e-12) {
                ResolvedRef r = resolveRef(*input, axisEdge);
                st.merge(r.status);
                if(!r.ok) return {input, st};
                BRepAdaptor_Curve c(TopoDS::Edge(r.shape));
                if(c.GetType() != GeomAbs_Line) return {input, Status::error("the rotation axis must be a straight edge")};
                const gp_Ax1 ax(c.Line().Location(), c.Line().Direction());
                frame.Rotate(ax, a);
                centre.Rotate(ax, a);
            }
            break;
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
