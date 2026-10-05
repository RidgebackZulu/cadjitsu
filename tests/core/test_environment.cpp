// Lighting environments for the renderers: the studio and daylight sets,
// their pre-filtered maps, irradiance and the BRDF table.
#include <doctest.h>

#include "render/Environment.h"

#include <cmath>

using namespace cad;
using namespace cad::rt;

TEST_CASE("studio and daylight: a key light from above, brighter above than below") {
    for(Lighting l : {Lighting::Studio, Lighting::Daylight}) {
        const Environment e = environment(l);
        CHECK(e.key.dir.z > 0.6f);
        CHECK(length(e.key.dir) == doctest::Approx(1.0f));
        const V3 irr = e.key.irradiance();
        CHECK(irr.x > 2.0f);
        CHECK(irr.x < 4.0f);
        CHECK(maxc(e.radiance({0, 0, 1})) > maxc(e.radiance({0, 0, -1})));
        // The key light shows only in what reflections see.
        CHECK(maxc(e.radianceWithKey(e.key.dir)) > 10.0f * maxc(e.radiance(e.key.dir)));
    }
    // Daylight's sky is blue.
    const V3 sky = environment(Lighting::Daylight).radiance(normalize(V3(1, 1, 3)));
    CHECK(sky.z > sky.x);
}

TEST_CASE("pre-filtered maps, irradiance and the BRDF table") {
    const Environment e = environment(Lighting::Studio);
    const EnvMaps m = bakeEnvMaps(e, 64, 4);
    REQUIRE(m.rgb.size() == 4);
    CHECK(m.rgb[0].size() == size_t(64 * 32 * 3));
    CHECK(m.rgb[3].size() == size_t(8 * 4 * 3));
    // Rougher levels are smoother: the brightest texel gets dimmer.
    auto peak = [&](int lv) {
        float p = 0;
        for(float v : m.rgb[size_t(lv)]) p = std::max(p, v);
        return p;
    };
    CHECK(peak(3) < peak(0));
    // Irradiance: upward faces get more light than downward ones, never negative.
    const V3 up = shIrradiance(m.sh, {0, 0, 1}), down = shIrradiance(m.sh, {0, 0, -1});
    CHECK(up.x > down.x);
    CHECK(down.x >= 0.0f);
    // A uniform white environment would give exactly 1: ours is of order 0.1-1.
    CHECK(up.x > 0.1f);
    CHECK(up.x < 2.0f);

    const std::vector<float> lut = brdfLut(16);
    for(size_t i = 0; i < lut.size(); i += 2) {
        CHECK(lut[i] >= 0.0f);
        CHECK(lut[i] + lut[i + 1] <= 1.02f);
    }
    // Smooth, head-on: almost all of F0 comes back.
    CHECK(lut[(0 * 16 + 15) * 2] == doctest::Approx(1.0f).epsilon(0.05));
}
