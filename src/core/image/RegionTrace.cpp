#include "image/RegionTrace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <unordered_map>

namespace cad {

namespace {

double colourDistance(const uint8_t *a, const double b[3]) {
    const double dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return std::sqrt(dr * dr + dg * dg + db * db) / (std::sqrt(3.0) * 255.0);
}

// Keeps only the biggest 4-connected piece of `m`.
Mask biggestPiece(const Mask &m) {
    Mask label(m.width, m.height), best(m.width, m.height);
    size_t bestCount = 0;
    std::vector<std::pair<int, int>> stack, piece;
    for(int y = 0; y < m.height; ++y)
        for(int x = 0; x < m.width; ++x) {
            if(!m.at(x, y) || label.at(x, y)) continue;
            piece.clear();
            stack = {{x, y}};
            label.set(x, y);
            while(!stack.empty()) {
                auto [cx, cy] = stack.back();
                stack.pop_back();
                piece.push_back({cx, cy});
                for(auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    const int nx = cx + dx, ny = cy + dy;
                    if(m.at(nx, ny) && !label.at(nx, ny)) {
                        label.set(nx, ny);
                        stack.push_back({nx, ny});
                    }
                }
            }
            if(piece.size() > bestCount) {
                bestCount = piece.size();
                std::fill(best.bits.begin(), best.bits.end(), 0);
                for(auto [px, py] : piece) best.set(px, py);
            }
        }
    return best;
}

} // namespace

size_t Mask::count() const { return size_t(std::count(bits.begin(), bits.end(), uint8_t(1))); }

Mask regionAt(const ImageView &img, int sx, int sy, double tolerance) {
    Mask m(img.width, img.height);
    if(sx < 0 || sy < 0 || sx >= img.width || sy >= img.height) return m;
    // The seed colour: the average around the click (a little noise-proof).
    double seed[3] = {0, 0, 0};
    int n = 0;
    for(int dy = -1; dy <= 1; ++dy)
        for(int dx = -1; dx <= 1; ++dx) {
            const int x = std::clamp(sx + dx, 0, img.width - 1), y = std::clamp(sy + dy, 0, img.height - 1);
            const uint8_t *p = img.at(x, y);
            for(int k = 0; k < 3; ++k) seed[k] += p[k];
            ++n;
        }
    for(double &c : seed) c /= n;
    std::vector<std::pair<int, int>> stack{{sx, sy}};
    m.set(sx, sy);
    while(!stack.empty()) {
        auto [x, y] = stack.back();
        stack.pop_back();
        for(auto [dx, dy] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            const int nx = x + dx, ny = y + dy;
            if(nx < 0 || ny < 0 || nx >= img.width || ny >= img.height || m.at(nx, ny)) continue;
            if(colourDistance(img.at(nx, ny), seed) > tolerance) continue;
            m.set(nx, ny);
            stack.push_back({nx, ny});
        }
    }
    return m;
}

Mask foreground(const ImageView &img, double tolerance) {
    Mask m(img.width, img.height);
    if(img.width < 3 || img.height < 3) return m;
    // The background: the median colour of the border.
    std::array<std::vector<int>, 3> border;
    auto add = [&](int x, int y) {
        const uint8_t *p = img.at(x, y);
        for(int k = 0; k < 3; ++k) border[size_t(k)].push_back(p[k]);
    };
    for(int x = 0; x < img.width; ++x) {
        add(x, 0);
        add(x, img.height - 1);
    }
    for(int y = 1; y + 1 < img.height; ++y) {
        add(0, y);
        add(img.width - 1, y);
    }
    double bg[3];
    for(int k = 0; k < 3; ++k) {
        auto &v = border[size_t(k)];
        std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
        bg[k] = v[v.size() / 2];
    }
    for(int y = 0; y < img.height; ++y)
        for(int x = 0; x < img.width; ++x)
            if(colourDistance(img.at(x, y), bg) > tolerance) m.set(x, y);
    return biggestPiece(m);
}

PixelBox boundingBox(const Mask &m) {
    PixelBox b{m.width, m.height, -1, -1};
    for(int y = 0; y < m.height; ++y)
        for(int x = 0; x < m.width; ++x)
            if(m.at(x, y)) {
                b.x0 = std::min(b.x0, x);
                b.y0 = std::min(b.y0, y);
                b.x1 = std::max(b.x1, x);
                b.y1 = std::max(b.y1, y);
            }
    return b;
}

std::vector<TracedLoop> traceLoops(const Mask &m, double minArea) {
    // Boundary edges along pixel sides, with the region on their right (in
    // y-down picture coordinates): walking them goes clockwise round the
    // region on screen. Corners are numbered y * (w + 1) + x.
    const int W = m.width + 1;
    auto id = [&](int x, int y) { return int64_t(y) * W + x; };
    std::unordered_multimap<int64_t, int64_t> next; // from corner -> to corner
    for(int y = 0; y < m.height; ++y)
        for(int x = 0; x < m.width; ++x) {
            if(!m.at(x, y)) continue;
            if(!m.at(x, y - 1)) next.insert({id(x, y), id(x + 1, y)});         // top, rightwards
            if(!m.at(x + 1, y)) next.insert({id(x + 1, y), id(x + 1, y + 1)}); // right, down
            if(!m.at(x, y + 1)) next.insert({id(x + 1, y + 1), id(x, y + 1)}); // bottom, leftwards
            if(!m.at(x - 1, y)) next.insert({id(x, y + 1), id(x, y)});         // left, up
        }
    auto pos = [&](int64_t c) { return Vec2(double(c % W), double(c / W)); };
    std::vector<TracedLoop> loops;
    while(!next.empty()) {
        auto first = next.begin();
        const int64_t start = first->first;
        std::vector<int64_t> corners{start};
        int64_t from = start, to = first->second;
        next.erase(first);
        while(to != start) {
            corners.push_back(to);
            // At a corner touched diagonally by two pieces, turn right (keep
            // to the piece being walked round).
            auto range = next.equal_range(to);
            if(range.first == range.second) break;
            auto pick = range.first;
            if(std::next(range.first) != range.second) {
                const Vec2 in = pos(to) - pos(from);
                double best = -1e9;
                for(auto it = range.first; it != range.second; ++it) {
                    const Vec2 out = pos(it->second) - pos(to);
                    // y down: a right turn on screen has a positive cross product.
                    const double score = in.cross(out);
                    if(score > best) {
                        best = score;
                        pick = it;
                    }
                }
            }
            from = to;
            to = pick->second;
            next.erase(pick);
        }
        if(corners.size() < 4) continue;
        // Cut the stair steps: the middles of the pixel edges.
        TracedLoop loop;
        const size_t n = corners.size();
        for(size_t i = 0; i < n; ++i) loop.points.push_back((pos(corners[i]) + pos(corners[(i + 1) % n])) * 0.5);
        // Drop points in the middle of straight runs.
        std::vector<Vec2> kept;
        for(size_t i = 0; i < loop.points.size(); ++i) {
            const Vec2 a = loop.points[(i + loop.points.size() - 1) % loop.points.size()], b = loop.points[i],
                       c = loop.points[(i + 1) % loop.points.size()];
            if(std::fabs((b - a).cross(c - b)) > 1e-12) kept.push_back(b);
        }
        loop.points = std::move(kept);
        double area = 0.0;
        for(size_t i = 0; i < loop.points.size(); ++i)
            area += loop.points[i].cross(loop.points[(i + 1) % loop.points.size()]);
        area *= 0.5; // positive: clockwise on screen (an outer boundary)
        if(std::fabs(area) < minArea) continue;
        loop.hole = area < 0;
        loop.area = std::fabs(area);
        loops.push_back(std::move(loop));
    }
    std::stable_sort(loops.begin(), loops.end(), [](const TracedLoop &a, const TracedLoop &b) {
        if(a.hole != b.hole) return !a.hole;
        return a.area > b.area;
    });
    return loops;
}

} // namespace cad
