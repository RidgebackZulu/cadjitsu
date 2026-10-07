#include "image/LensModel.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cad {

namespace {

struct Frame {
    Vec2 centre;
    double unit = 1.0; // half the diagonal
};

Frame frameOf(int width, int height) {
    return {Vec2(width * 0.5, height * 0.5), std::max(1.0, 0.5 * std::hypot(double(width), double(height)))};
}

double radialScale(const LensDistortion &l, double r2) { return 1.0 + l.k1 * r2 + l.k2 * r2 * r2; }

// A lens maps radii one to one over the picture (and a bit beyond): the seen
// radius keeps growing with the true one.
bool sensible(const LensDistortion &l) {
    if(std::fabs(l.k1) > 1.0 || std::fabs(l.k2) > 1.0) return false;
    for(int i = 0; i <= 24; ++i) {
        const double r = 1.2 * i / 24, r2 = r * r;
        if(1.0 + 3.0 * l.k1 * r2 + 5.0 * l.k2 * r2 * r2 < 0.2) return false;
    }
    return true;
}

// How far from straight each line is: the spread across its best-fit line
// over the spread along it (0: straight), summed.
double bend(const std::vector<std::vector<Vec2>> &lines, const LensDistortion &l, int w, int h) {
    double total = 0;
    for(const auto &line : lines) {
        Vec2 c;
        std::vector<Vec2> pts;
        pts.reserve(line.size());
        for(const Vec2 &p : line) pts.push_back(undistortPixel(l, p, w, h));
        for(const Vec2 &p : pts) c = c + p;
        c = c / double(pts.size());
        double sxx = 0, sxy = 0, syy = 0;
        for(const Vec2 &p : pts) {
            const Vec2 r = p - c;
            sxx += r.x * r.x;
            sxy += r.x * r.y;
            syy += r.y * r.y;
        }
        const double mean = 0.5 * (sxx + syy), diff = std::sqrt(0.25 * (sxx - syy) * (sxx - syy) + sxy * sxy);
        const double big = mean + diff, small = std::max(0.0, mean - diff);
        if(big > 1e-12) total += small / big;
    }
    return total;
}

// Nelder-Mead over (k1, k2), or k1 alone.
LensDistortion minimise(const std::vector<std::vector<Vec2>> &lines, int w, int h, bool withK2) {
    auto cost = [&](const std::array<double, 2> &k) {
        const LensDistortion l{k[0], withK2 ? k[1] : 0.0};
        if(!sensible(l)) return 1e9;
        return bend(lines, l, w, h) + 1e-7 * (k[0] * k[0] + k[1] * k[1]);
    };
    const int dims = withK2 ? 2 : 1;
    std::vector<std::array<double, 2>> simplex{{0.0, 0.0}, {0.08, 0.0}};
    if(withK2) simplex.push_back({0.0, 0.08});
    std::vector<double> f;
    for(const auto &s : simplex) f.push_back(cost(s));
    for(int iter = 0; iter < 400; ++iter) {
        std::vector<int> order(simplex.size());
        for(size_t i = 0; i < order.size(); ++i) order[i] = int(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return f[size_t(a)] < f[size_t(b)]; });
        std::vector<std::array<double, 2>> s2;
        std::vector<double> f2;
        for(int i : order) {
            s2.push_back(simplex[size_t(i)]);
            f2.push_back(f[size_t(i)]);
        }
        simplex = s2;
        f = f2;
        double spread = 0;
        for(size_t i = 1; i < simplex.size(); ++i)
            spread = std::max(spread, std::hypot(simplex[i][0] - simplex[0][0], simplex[i][1] - simplex[0][1]));
        if(spread < 1e-7) break;
        std::array<double, 2> centroid{0, 0};
        for(int i = 0; i < dims; ++i)
            for(int d = 0; d < 2; ++d) centroid[size_t(d)] += simplex[size_t(i)][size_t(d)] / dims;
        const auto at = [&](double t) {
            std::array<double, 2> p;
            for(int d = 0; d < 2; ++d)
                p[size_t(d)] = centroid[size_t(d)] + t * (simplex.back()[size_t(d)] - centroid[size_t(d)]);
            return p;
        };
        const auto reflected = at(-1.0);
        const double fr = cost(reflected);
        if(fr < f.front()) {
            const auto expanded = at(-2.0);
            const double fe = cost(expanded);
            simplex.back() = fe < fr ? expanded : reflected;
            f.back() = std::min(fe, fr);
        } else if(fr < f[f.size() - 2]) {
            simplex.back() = reflected;
            f.back() = fr;
        } else {
            const auto contracted = at(0.5);
            const double fc = cost(contracted);
            if(fc < f.back()) {
                simplex.back() = contracted;
                f.back() = fc;
            } else {
                for(size_t i = 1; i < simplex.size(); ++i) {
                    for(int d = 0; d < 2; ++d)
                        simplex[i][size_t(d)] = simplex[0][size_t(d)] + 0.5 * (simplex[i][size_t(d)] - simplex[0][size_t(d)]);
                    f[i] = cost(simplex[i]);
                }
            }
        }
    }
    size_t best = 0;
    for(size_t i = 1; i < f.size(); ++i)
        if(f[i] < f[best]) best = i;
    return {simplex[best][0], withK2 ? simplex[best][1] : 0.0};
}

} // namespace

Vec2 distortPixel(const LensDistortion &lens, Vec2 p, int width, int height) {
    const Frame fr = frameOf(width, height);
    const Vec2 x = (p - fr.centre) / fr.unit;
    return fr.centre + x * (radialScale(lens, x.dot(x)) * fr.unit);
}

Vec2 undistortPixel(const LensDistortion &lens, Vec2 p, int width, int height) {
    if(lens.none()) return p;
    const Frame fr = frameOf(width, height);
    const Vec2 x = (p - fr.centre) / fr.unit;
    const double rd = x.length();
    if(rd < 1e-12) return p;
    // Solve ru (1 + k1 ru^2 + k2 ru^4) = rd by Newton's method.
    double ru = rd;
    for(int i = 0; i < 30; ++i) {
        const double r2 = ru * ru;
        const double g = ru * radialScale(lens, r2) - rd;
        const double dg = 1.0 + 3.0 * lens.k1 * r2 + 5.0 * lens.k2 * r2 * r2;
        if(std::fabs(dg) < 1e-9) break;
        const double next = ru - g / dg;
        if(std::fabs(next - ru) < 1e-13) {
            ru = next;
            break;
        }
        ru = std::max(0.0, next);
    }
    return fr.centre + x * (ru / rd * fr.unit);
}

std::optional<LensDistortion> estimateLens(const std::vector<std::vector<Vec2>> &lines, int width, int height) {
    std::vector<std::vector<Vec2>> use;
    size_t points = 0;
    for(const auto &l : lines)
        if(l.size() >= 3) {
            use.push_back(l);
            points += l.size();
        }
    if(use.empty() || width <= 0 || height <= 0) return std::nullopt;
    // k2 only with enough to pin it down: two lines and a dozen points.
    const bool withK2 = use.size() >= 2 && points >= 12;
    LensDistortion l = minimise(use, width, height, withK2);
    if(!sensible(l)) return std::nullopt;
    // No better than leaving it: no lens to correct.
    if(bend(use, l, width, height) >= bend(use, {}, width, height)) return LensDistortion{};
    return l;
}

double lineStraightness(const LensDistortion &lens, const std::vector<std::vector<Vec2>> &lines, int width, int height) {
    double worst = 0;
    for(const auto &line : lines) {
        if(line.size() < 3) continue;
        std::vector<Vec2> pts;
        for(const Vec2 &p : line) pts.push_back(undistortPixel(lens, p, width, height));
        // Distance from the chord of the end points, which a clicked edge spans.
        const Vec2 a = pts.front(), b = pts.back(), d = (b - a).normalized();
        for(const Vec2 &p : pts) worst = std::max(worst, std::fabs((p - a).cross(d)));
    }
    return worst;
}

std::vector<std::uint8_t> undistortImage(const ImageView &photo, const LensDistortion &lens) {
    const int w = photo.width, h = photo.height;
    std::vector<std::uint8_t> out(size_t(w) * size_t(h) * 4, 0);
    auto texel = [&](int x, int y, int c) -> double { return photo.at(x, y)[c]; };
    for(int y = 0; y < h; ++y) {
        for(int x = 0; x < w; ++x) {
            const Vec2 s = distortPixel(lens, Vec2(x + 0.5, y + 0.5), w, h) - Vec2(0.5, 0.5);
            if(s.x < -0.5 || s.y < -0.5 || s.x > w - 0.5 || s.y > h - 0.5) continue;
            const int x0 = std::clamp(int(std::floor(s.x)), 0, w - 1), y0 = std::clamp(int(std::floor(s.y)), 0, h - 1);
            const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const double fx = std::clamp(s.x - x0, 0.0, 1.0), fy = std::clamp(s.y - y0, 0.0, 1.0);
            std::uint8_t *o = out.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
            for(int c = 0; c < 4; ++c) {
                const double v = (texel(x0, y0, c) * (1 - fx) + texel(x1, y0, c) * fx) * (1 - fy) +
                                 (texel(x0, y1, c) * (1 - fx) + texel(x1, y1, c) * fx) * fy;
                o[c] = std::uint8_t(std::clamp(int(std::lround(v)), 0, 255));
            }
        }
    }
    return out;
}

} // namespace cad
