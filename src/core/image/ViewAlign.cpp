#include "image/ViewAlign.h"

#include <algorithm>
#include <cmath>

namespace cad {

namespace {

// The axes a view shows: across, then up.
void axesOf(ViewSide s, int &across, int &up) {
    switch(s) {
    case ViewSide::Front: across = 0, up = 2; break;
    case ViewSide::Side: across = 1, up = 2; break;
    case ViewSide::Top: across = 0, up = 1; break;
    }
}

const char *sideName(ViewSide s) {
    switch(s) {
    case ViewSide::Front: return "front";
    case ViewSide::Side: return "side";
    case ViewSide::Top: return "top";
    }
    return "";
}

} // namespace

const char *viewPlaneName(ViewSide side) {
    switch(side) {
    case ViewSide::Front: return "xz";
    case ViewSide::Side: return "yz";
    case ViewSide::Top: return "xy";
    }
    return "xy";
}

ViewsResult alignViews(const std::vector<ViewInput> &views, int axis, double mm) {
    ViewsResult r;
    if(views.empty()) {
        r.error = "give at least one view";
        return r;
    }
    if(axis < 0 || axis > 2 || !(mm > 0)) {
        r.error = "give the real size of the part along X, Y or Z (more than 0)";
        return r;
    }
    for(size_t i = 0; i < views.size(); ++i) {
        for(size_t j = 0; j < i; ++j)
            if(views[i].side == views[j].side) {
                r.error = std::string("two ") + sideName(views[i].side) + " views";
                return r;
            }
        if(views[i].box.empty() || views[i].box.width() < 4 || views[i].box.height() < 4) {
            r.error = std::string("no part found in the ") + sideName(views[i].side) + " view";
            return r;
        }
    }
    // The part's size along each axis: from the known one, through the
    // proportions each view shows (several routes are averaged).
    std::optional<double> known[3];
    known[axis] = mm;
    for(int pass = 0; pass < 3; ++pass) {
        double sum[3] = {0, 0, 0};
        int count[3] = {0, 0, 0};
        for(const ViewInput &v : views) {
            int a = 0, u = 0;
            axesOf(v.side, a, u);
            const double ratio = double(v.box.height()) / v.box.width(); // up / across
            if(known[a] && !known[u]) {
                sum[u] += *known[a] * ratio;
                ++count[u];
            } else if(known[u] && !known[a]) {
                sum[a] += *known[u] / ratio;
                ++count[a];
            }
        }
        for(int k = 0; k < 3; ++k)
            if(count[k] && !known[k]) known[k] = sum[k] / count[k];
    }
    // Each view's scale, from both its axes where known.
    for(const ViewInput &v : views) {
        int a = 0, u = 0;
        axesOf(v.side, a, u);
        if(!known[a] && !known[u]) {
            r.error = std::string("the ") + sideName(v.side) + " view does not show the measured size, nor link to a view that does";
            return r;
        }
        const double sa = known[a] ? *known[a] / v.box.width() : 0.0, su = known[u] ? *known[u] / v.box.height() : 0.0;
        const double scale = sa > 0 && su > 0 ? 0.5 * (sa + su) : std::max(sa, su);
        if(sa > 0 && su > 0 && std::fabs(sa - su) > 0.05 * scale)
            r.warnings.push_back(std::string("the ") + sideName(v.side) + " view's proportions differ from the others by " +
                                 std::to_string(int(std::lround(100 * std::fabs(sa - su) / scale))) +
                                 "% (a photo in perspective, or not square on?)");
        // The part's box centred across, standing on z = 0 (front and side)
        // or centred on the origin (top).
        const Vec2 target = v.side == ViewSide::Top ? Vec2(0, 0) : Vec2(0, v.box.height() * scale / 2);
        const double bx = 0.5 * (v.box.x0 + v.box.x1 + 1), by = 0.5 * (v.box.y0 + v.box.y1 + 1);
        const Vec2 boxFromCentre((bx - v.width / 2.0) * scale, -(by - v.height / 2.0) * scale);
        r.views.push_back({v.side, scale, target - boxFromCentre});
    }
    for(int k = 0; k < 3; ++k) r.size[k] = known[k].value_or(0.0);
    r.ok = true;
    return r;
}

} // namespace cad
