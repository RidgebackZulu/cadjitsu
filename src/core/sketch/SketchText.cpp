#include "sketch/SketchText.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cad {

namespace {

double signedArea(const std::vector<Vec2> &pts) {
    double a = 0.0;
    for(size_t i = 0; i < pts.size(); ++i) a += pts[i].cross(pts[(i + 1) % pts.size()]);
    return a * 0.5;
}

bool inside(const std::vector<Vec2> &poly, Vec2 p) {
    bool in = false;
    for(size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2 a = poly[i], b = poly[j];
        if((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

// A point well inside the region between `outer` and `holes`: the middle of
// the widest inside stretch along a few horizontal lines.
Vec2 interiorPoint(const std::vector<Vec2> &outer, const std::vector<std::vector<Vec2>> &holes) {
    double y0 = 1e300, y1 = -1e300;
    for(const Vec2 &p : outer) {
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    Vec2 best = outer.front();
    double bestW = -1.0;
    for(int k = 1; k < 16; ++k) {
        const double y = y0 + (y1 - y0) * (k + 0.37) / 16.0;
        std::vector<double> xs;
        auto cross = [&](const std::vector<Vec2> &poly) {
            for(size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
                const Vec2 a = poly[i], b = poly[j];
                if((a.y > y) != (b.y > y)) xs.push_back(a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y));
            }
        };
        cross(outer);
        for(const auto &h : holes) cross(h);
        std::sort(xs.begin(), xs.end());
        for(size_t i = 0; i + 1 < xs.size(); i += 2)
            if(xs[i + 1] - xs[i] > bestW) {
                bestW = xs[i + 1] - xs[i];
                best = {(xs[i] + xs[i + 1]) * 0.5, y};
            }
    }
    return best;
}

ProfileLoop loopOf(const std::vector<Vec2> &pts, const std::string &keyBase) {
    ProfileLoop loop;
    for(size_t k = 0; k < pts.size(); ++k) {
        ProfileSeg s;
        s.isArc = false;
        s.p0 = pts[k];
        s.p1 = pts[(k + 1) % pts.size()];
        s.key = keyBase + "." + std::to_string(k);
        s.curveId = 0; // not a sketch curve: pieces are never merged into one edge
        loop.segs.push_back(s);
    }
    loop.area = signedArea(pts);
    return loop;
}

std::vector<Vec2> reversed(std::vector<Vec2> v) {
    std::reverse(v.begin(), v.end());
    return v;
}

} // namespace

TextStyle textStyleOf(const SkEntity &e) {
    TextStyle st;
    st.font = e.font;
    st.bold = e.bold;
    st.italic = e.italic;
    st.size = e.size;
    st.mirror = e.mirror;
    return st;
}

SketchTextLetters sketchTextLetters(const Sketch &sketch, const SkEntity &e) {
    SketchTextLetters out;
    const auto shape = buildText(e.text, textStyleOf(e));
    out.ok = shape->ok;
    out.error = shape->error;
    out.warning = shape->warning;
    if(!shape->ok) return out;
    const Vec2 o = sketch.pointPos(e.a);
    const double a = e.angle * kPi / 180.0, c = std::cos(a), s = std::sin(a);
    auto place = [&](Vec2 p) { return o + Vec2(p.x * c - p.y * s, p.x * s + p.y * c); };
    for(const auto &region : shape->regions) {
        std::vector<std::vector<Vec2>> piece;
        for(size_t li : region) {
            std::vector<Vec2> loop;
            loop.reserve(shape->loops[li].size());
            for(const Vec2 &p : shape->loops[li]) loop.push_back(place(p));
            piece.push_back(std::move(loop));
        }
        out.pieces.push_back(std::move(piece));
    }
    return out;
}

std::vector<Profile> sketchProfiles(const Sketch &sketch) {
    std::vector<Profile> profiles = buildProfiles(sketchCurves(sketch)).profiles;
    const size_t fromCurves = profiles.size();
    for(const auto &e : sketch.entities) {
        if(!e.isText() || e.construction) continue;
        const SketchTextLetters letters = sketchTextLetters(sketch, e);
        for(size_t pi = 0; pi < letters.pieces.size(); ++pi) {
            const auto &piece = letters.pieces[pi];
            const std::string key = "t" + std::to_string(e.id) + "." + std::to_string(pi);
            Profile p;
            p.key = key;
            p.outer = loopOf(piece[0], key + ".o");
            std::vector<std::vector<Vec2>> holes(piece.begin() + 1, piece.end());
            for(size_t h = 0; h < holes.size(); ++h) p.holes.push_back(loopOf(holes[h], key + ".h" + std::to_string(h)));
            p.area = p.outer.area;
            for(const auto &h : p.holes) p.area += h.area;
            p.sample = interiorPoint(piece[0], holes);
            // The smallest region of the curves wholly holding the letter.
            Profile *holder = nullptr;
            for(size_t i = 0; i < fromCurves; ++i) {
                Profile &q = profiles[i];
                const bool all = std::all_of(piece[0].begin(), piece[0].end(), [&](Vec2 v) { return q.contains(v); });
                if(all && (!holder || std::fabs(q.area) < std::fabs(holder->area))) holder = &q;
            }
            if(holder) {
                // The letter is cut out of it; its insides are regions of their own.
                holder->holes.push_back(loopOf(reversed(piece[0]), key + ".x"));
                holder->area -= std::fabs(p.outer.area);
                for(size_t h = 0; h < holes.size(); ++h) {
                    Profile in;
                    in.key = key + ".in" + std::to_string(h);
                    in.outer = loopOf(reversed(holes[h]), in.key);
                    in.area = in.outer.area;
                    in.sample = interiorPoint(in.outer.polygon(), {});
                    profiles.push_back(std::move(in));
                }
            }
            profiles.push_back(std::move(p));
        }
    }
    return profiles;
}

int textOfProfile(const Profile &p) {
    if(p.key.size() < 2 || p.key[0] != 't') return 0;
    return std::atoi(p.key.c_str() + 1);
}

} // namespace cad
