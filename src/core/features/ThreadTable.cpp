#include "features/ThreadTable.h"

#include <cmath>

namespace cad {

namespace {

constexpr double kIn = 25.4;

std::vector<ThreadSpec> build() {
    std::vector<ThreadSpec> t;
    // ISO metric coarse (tap drill = D - P).
    const std::pair<double, double> coarse[] = {{2, 0.4},  {2.5, 0.45}, {3, 0.5},  {4, 0.7},   {5, 0.8},  {6, 1.0},  {8, 1.25},
                                                {10, 1.5}, {12, 1.75},  {14, 2.0}, {16, 2.0},  {20, 2.5}, {24, 3.0}};
    for(const auto &[d, p] : coarse) {
        char n[32];
        std::snprintf(n, sizeof n, "M%g", d);
        t.push_back({n, "ISO", d, p, d - p});
    }
    const std::pair<double, double> fine[] = {{8, 1.0}, {10, 1.25}, {12, 1.5}, {16, 1.5}, {20, 1.5}, {24, 2.0}};
    for(const auto &[d, p] : fine) {
        char n[32];
        std::snprintf(n, sizeof n, "M%gx%g", d, p);
        t.push_back({n, "ISO fine", d, p, d - p});
    }
    // Unified threads: size, diameter (in), threads per inch, tap drill (in).
    struct U {
        const char *size;
        double d;
        int tpi;
        double drill;
    };
    const U unc[] = {{"#4-40", 0.112, 40, 0.089},     {"#6-32", 0.138, 32, 0.1065},   {"#8-32", 0.164, 32, 0.136},
                     {"#10-24", 0.190, 24, 0.1495},   {"1/4-20", 0.250, 20, 0.201},   {"5/16-18", 0.3125, 18, 0.257},
                     {"3/8-16", 0.375, 16, 0.3125},   {"7/16-14", 0.4375, 14, 0.368}, {"1/2-13", 0.5, 13, 0.4219},
                     {"5/8-11", 0.625, 11, 0.5312},   {"3/4-10", 0.75, 10, 0.6562},   {"1-8", 1.0, 8, 0.875}};
    const U unf[] = {{"#4-48", 0.112, 48, 0.0935},   {"#6-40", 0.138, 40, 0.113},    {"#8-36", 0.164, 36, 0.136},
                     {"#10-32", 0.190, 32, 0.159},   {"1/4-28", 0.250, 28, 0.213},   {"5/16-24", 0.3125, 24, 0.272},
                     {"3/8-24", 0.375, 24, 0.332},   {"7/16-20", 0.4375, 20, 0.391}, {"1/2-20", 0.5, 20, 0.453},
                     {"5/8-18", 0.625, 18, 0.578},   {"3/4-16", 0.75, 16, 0.688},    {"1-12", 1.0, 12, 0.922}};
    for(const U &u : unc) t.push_back({std::string(u.size) + " UNC", "UNC", u.d * kIn, kIn / u.tpi, u.drill * kIn});
    for(const U &u : unf) t.push_back({std::string(u.size) + " UNF", "UNF", u.d * kIn, kIn / u.tpi, u.drill * kIn});
    return t;
}

} // namespace

const std::vector<ThreadSpec> &threadTable() {
    static const std::vector<ThreadSpec> table = build();
    return table;
}

const ThreadSpec *findThread(const std::string &name) {
    for(const ThreadSpec &s : threadTable())
        if(s.name == name) return &s;
    // "M6x1" names the coarse M6; "1/4-20" without its series.
    for(const ThreadSpec &s : threadTable()) {
        char coarse[32];
        std::snprintf(coarse, sizeof coarse, "M%gx%g", s.major, s.pitch);
        if(s.standard == "ISO" && name == coarse) return &s;
        if(s.name.rfind(name + " ", 0) == 0) return &s;
    }
    return nullptr;
}

const ThreadSpec *nearestThread(double diameter, bool internal, const std::string &standard) {
    // How far a size is from the cylinder: a hole might be drilled at the tap
    // size or at the minor diameter, a boss is turned to the major one.
    auto error = [&](const ThreadSpec &s) {
        return internal ? std::min(std::fabs(s.tapDrill - diameter), std::fabs(s.minor() - diameter))
                        : std::fabs(s.major - diameter);
    };
    const ThreadSpec *metric = nullptr, *imperial = nullptr;
    for(const ThreadSpec &s : threadTable()) {
        if(!standard.empty() && s.standard != standard) continue;
        const bool isMetric = s.standard.rfind("ISO", 0) == 0;
        const ThreadSpec *&best = isMetric ? metric : imperial;
        // Coarse / UNC first on ties (they come first in the table).
        if(!best || error(s) < error(*best) - 1e-6) best = &s;
    }
    // Metric unless an inch size fits clearly better (a 6.8 mm hole is an M8,
    // not a 5/16-24 whose minor diameter happens to be 6.79 mm).
    const ThreadSpec *best = metric;
    if(!best || (imperial && error(*imperial) < error(*metric) - 0.1)) best = imperial;
    // Nothing within 15 % is not a match.
    if(best && error(*best) > 0.15 * diameter) return nullptr;
    return best;
}

} // namespace cad
