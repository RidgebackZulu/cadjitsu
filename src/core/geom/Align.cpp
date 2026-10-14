#include "geom/Align.h"

#include <gp_Ax1.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

constexpr double kPiA = 3.14159265358979323846;

// The turn taking direction `a` to `b`, about an axis through `pivot`.
gp_Trsf turnOnto(const gp_Dir &a, const gp_Dir &b, const gp_Pnt &pivot, const std::optional<gp_Dir> &upright) {
    gp_Trsf t;
    const double c = a.Dot(b);
    if(c > 1 - 1e-12) return t;
    gp_Dir axis;
    double angle;
    if(c < -1 + 1e-12) {
        // Exactly opposite: half a turn about any axis square to `a`; the
        // upright one if it is.
        if(upright && std::fabs(upright->Dot(a)) < 1e-9) axis = *upright;
        else {
            const gp_Vec helper = std::fabs(a.X()) < 0.9 ? gp_Vec(1, 0, 0) : gp_Vec(0, 1, 0);
            axis = gp_Dir(gp_Vec(a).Crossed(helper));
        }
        angle = kPiA;
    } else {
        axis = gp_Dir(gp_Vec(a).Crossed(gp_Vec(b)));
        angle = std::acos(std::clamp(c, -1.0, 1.0));
    }
    t.SetRotation(gp_Ax1(pivot, axis), angle);
    return t;
}

gp_Pnt footOnLine(const gp_Pnt &p, const gp_Pnt &on, const gp_Dir &d) {
    return on.Translated(gp_Vec(d) * gp_Vec(on, p).Dot(gp_Vec(d)));
}

gp_Pnt footOnPlane(const gp_Pnt &p, const gp_Pnt &on, const gp_Dir &n) {
    return p.Translated(-gp_Vec(n) * gp_Vec(on, p).Dot(gp_Vec(n)));
}

gp_Trsf shift(const gp_Pnt &a, const gp_Pnt &b) {
    gp_Trsf t;
    t.SetTranslation(a, b);
    return t;
}

} // namespace

const char *alignKindName(AlignKind k) {
    return k == AlignKind::Point ? "point" : k == AlignKind::Line ? "line" : "face";
}

std::optional<gp_Trsf> alignTransform(const AlignAnchor &from, const AlignAnchor &to, bool flip, double angle,
                                      std::string &why, std::optional<gp_Dir> upright) {
    // Anything onto a point: its point onto that point.
    if(to.kind == AlignKind::Point) return shift(from.p, to.p);
    if(from.kind == AlignKind::Point) {
        // Slid straight onto the line or plane.
        return shift(from.p, to.kind == AlignKind::Line ? footOnLine(from.p, to.p, to.d) : footOnPlane(from.p, to.p, to.d));
    }
    if(from.kind == AlignKind::Plane && to.kind == AlignKind::Line) {
        why = "a face cannot go onto a line: pick a face (or a point) to go onto, or pick an edge to move";
        return std::nullopt;
    }
    gp_Dir dir;       // where `from`'s direction ends up
    gp_Pnt target;    // where `from`'s point ends up
    gp_Trsf turn;
    if(from.kind == AlignKind::Line && to.kind == AlignKind::Line) {
        dir = flip ? to.d.Reversed() : to.d;
        turn = turnOnto(from.d, dir, from.p, upright);
        target = to.bounded ? to.p : footOnLine(from.p, to.p, to.d);
    } else if(from.kind == AlignKind::Plane) { // onto a plane
        dir = flip ? to.d : to.d.Reversed();
        turn = turnOnto(from.d, dir, from.p, upright);
        target = to.p;
        dir = to.d; // turned about the shared normal
    } else { // a line laid in a plane
        const gp_Vec along = gp_Vec(from.d) - gp_Vec(to.d) * gp_Vec(from.d).Dot(gp_Vec(to.d));
        if(along.Magnitude() < 1e-9) {
            why = "the line stands square to the face, so it has no way to lie in it: pick another line or face";
            return std::nullopt;
        }
        const gp_Dir inPlane(flip ? along.Reversed() : along);
        turn = turnOnto(from.d, inPlane, from.p, upright);
        target = footOnPlane(from.p, to.p, to.d);
        dir = to.d;
    }
    gp_Trsf t = shift(from.p, target) * turn;
    if(std::fabs(angle) > 1e-12) {
        gp_Trsf spin;
        spin.SetRotation(gp_Ax1(target, dir), angle);
        t = spin * t;
    }
    return t;
}

std::string describeAlign(AlignKind from, AlignKind to) {
    using K = AlignKind;
    if(to == K::Point) {
        if(from == K::Point) return "It moves so the orange point sits on the blue point.";
        if(from == K::Line) return "It moves (without turning) so the orange line's end sits on the blue point.";
        return "It moves (without turning) so the orange face's centre sits on the blue point.";
    }
    if(from == K::Point)
        return to == K::Line ? "It slides straight across until the orange point lies on the blue line."
                             : "It slides straight across until the orange point lies on the blue face.";
    if(from == K::Line && to == K::Line)
        return "It turns to lie along the blue line, the orange end you clicked on the blue end. Flip "
               "turns it end for end; Angle spins it about the line.";
    if(from == K::Plane && to == K::Plane)
        return "The faces meet flat, centre to centre. Flip turns it to face the same way instead; "
               "Angle spins it about the faces' normal.";
    if(from == K::Line && to == K::Plane)
        return "It turns to lie flat in the face. Flip reverses the line; Angle spins it about the face's "
               "normal.";
    return "A face cannot go onto a line: pick a face or a point to go onto.";
}

} // namespace cad
