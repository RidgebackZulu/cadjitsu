#pragma once

#include "base/Vec2.h"
#include "sketch/Sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>

#include <string>
#include <vector>

namespace cad {

enum class MeasureUnit { Length, Area, Volume, Angle, Text };

// One labelled value for the selection statistics shown in the status bar.
struct Measurement {
    std::string label;
    double value = 0.0; // base units: mm, mm^2, mm^3, rad
    MeasureUnit unit = MeasureUnit::Length;
    std::string text;   // for MeasureUnit::Text

    std::string format(int decimals = 2) const;
};

std::vector<Measurement> measureFace(const TopoDS_Face &face);
std::vector<Measurement> measureEdge(const TopoDS_Edge &edge);
std::vector<Measurement> measureVertex(const TopoDS_Vertex &vertex);
std::vector<Measurement> measureBody(const TopoDS_Shape &solid);

// Sketch entities (2D, sketch units).
std::vector<Measurement> measureSketchEntity(const Sketch &sketch, int entityId);

// "Label: value unit | Label: value unit".
std::string formatMeasurements(const std::vector<Measurement> &m, int decimals = 2);

} // namespace cad
