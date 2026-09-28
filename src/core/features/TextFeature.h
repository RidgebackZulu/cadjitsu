#pragma once

#include "doc/Feature.h"
#include "text/TextShape.h"
#include "topo/Refs.h"

#include <Geom_Surface.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax3.hxx>

namespace cad {

enum class TextDirection { Engrave, Emboss };

const char *toString(TextDirection d);
TextDirection textDirectionFromString(const std::string &s);

// Where text goes on a face: a 2D frame (x, y in mm) laid on it.
//  - Planar faces: x and y in the face's plane from the world origin projected
//    onto it; on a flat top face they are world X and Y, on an upright face y
//    is up and x runs to the right seen from outside.
//  - Curved faces: mm along the surface from the middle of the face, x along
//    the surface's first direction (around a cylinder), y along its second
//    (along its axis), seen from outside.
// Text laid out in this frame follows the surface (it unrolls exactly onto
// cylinders and cones).
struct FaceTextFrame {
    Handle(Geom_Surface) surface; // a plane (planar faces) or the face's surface
    bool planar = false;
    gp_Ax3 plane;                 // planar: the frame (x, y, outward z)
    double u0 = 0, v0 = 0;        // curved: surface parameters of (0, 0)
    double su = 1, sv = 1;        // curved: mm per unit of u and of v there
    double sign = 1;              // curved: -1 when x runs against u (a reversed face)
    double flip = 1;              // curved: -1 to turn the text upright (y against v)

    // Surface parameters of a frame point.
    gp_Pnt2d uv(double x, double y) const;
    gp_Pnt point(double x, double y, double offset = 0.0) const; // `offset` outwards
    gp_Dir normal(double x, double y) const;                     // outwards
    // The frame directions at a point (x along the text, y up it, both on the surface).
    void axes(double x, double y, gp_Dir &xDir, gp_Dir &yDir) const;
    // The frame point nearest a 3D point.
    bool locate(const gp_Pnt &p, double &x, double &y) const;
};

bool faceTextFrame(const TopoDS_Face &face, FaceTextFrame &out, std::string &error);

// Text engraved into (cut) or embossed onto (raised from) a face of a body.
class TextFeature : public Feature {
public:
    TopoRef face;
    std::string text;
    std::string font = "DejaVu Sans";
    bool bold = false, italic = false;
    bool mirror = false;          // Reverse: the letters mirrored
    ParamSlot size;               // letter height (the font's em size), mm
    ParamSlot letterSpacing;      // extra space between letters, mm
    ParamSlot lineSpacing;        // line pitch, as a multiple of the size
    ParamSlot x, y;               // the text's middle, in the face's frame
    ParamSlot rotation;           // about the face normal (anticlockwise seen from outside)
    ParamSlot depth;              // how deep it is cut or how high it stands
    TextDirection direction = TextDirection::Engrave;

    FeatureType type() const override { return FeatureType::Text; }
    std::shared_ptr<Feature> clone() const override { return std::make_shared<TextFeature>(*this); }
    std::vector<ParamDef> params() const override;
    json dataToJson() const override;
    void dataFromJson(const json &j) override;
    FeatureResult compute(const StatePtr &input, const ComputeContext &ctx) const override;
};

// The text laid out on a face, as the feature would place it.
struct TextPlacementInput {
    std::string text;
    TextStyle style;
    double x = 0, y = 0, rotation = 0; // rotation in radians
};

struct TextOnFace {
    bool ok = false;
    std::string error, warning;
    FaceTextFrame frame;
    // Letter outlines on the surface (closed polylines, first point not repeated).
    std::vector<std::vector<gp_Pnt>> outlines;
    // The same outlines in the face's frame (x, y).
    std::vector<std::vector<std::pair<double, double>>> outlinesXY;
    // The text's extent in its own frame (before placing), mm.
    double width = 0, height = 0;
};

// Outlines of the text on the face (for the canvas while it is being placed).
TextOnFace layTextOnFace(const TopoDS_Face &face, const TextPlacementInput &in);

// The solids the letters make: from `below` under the surface to `above`
// over it (mm), following it. Fails (with `error`) if they cannot be made.
bool textSolids(const FaceTextFrame &frame, const TextShape &shape, const TextPlacementInput &in, double below,
                double above, std::vector<TopoDS_Shape> &solids, std::string &error);

} // namespace cad
