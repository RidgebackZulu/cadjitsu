// The path tracer: it sees what is there, it is repeatable, shadows and light
// behave, and semitransparent plastic tints and dims what is behind it.
#include <doctest.h>

#include "render/Environment.h"
#include "render/PathTracer.h"

#include <cmath>

using namespace cad;
using namespace cad::rt;

namespace {

using M4 = std::array<float, 16>; // column-major

float luminance(const V3 &c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

M4 mul(const M4 &a, const M4 &b) {
    M4 r{};
    for(int c = 0; c < 4; ++c)
        for(int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for(int k = 0; k < 4; ++k) s += a[size_t(k * 4 + rr)] * b[size_t(c * 4 + k)];
            r[size_t(c * 4 + rr)] = s;
        }
    return r;
}

M4 invert(const M4 &m) {
    // Gauss-Jordan on a row-major copy.
    double a[4][8];
    for(int r = 0; r < 4; ++r)
        for(int c = 0; c < 4; ++c) {
            a[r][c] = m[size_t(c * 4 + r)];
            a[r][c + 4] = r == c ? 1 : 0;
        }
    for(int c = 0; c < 4; ++c) {
        int p = c;
        for(int r = c + 1; r < 4; ++r)
            if(std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        for(int k = 0; k < 8; ++k) std::swap(a[c][k], a[p][k]);
        const double d = a[c][c];
        for(int k = 0; k < 8; ++k) a[c][k] /= d;
        for(int r = 0; r < 4; ++r)
            if(r != c) {
                const double f = a[r][c];
                for(int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
            }
    }
    M4 out{};
    for(int r = 0; r < 4; ++r)
        for(int c = 0; c < 4; ++c) out[size_t(c * 4 + r)] = float(a[r][c + 4]);
    return out;
}

// A camera at `eye` looking at `at` (Z up), 40 degree field of view.
TraceCamera camera(V3 eye, V3 at, int w, int h) {
    const V3 f = normalize(at - eye), s = normalize(cross(f, V3(0, 0, 1))), u = cross(s, f);
    const M4 view{s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
                  -dot(s, eye), -dot(u, eye), dot(f, eye), 1};
    const float n = 1.0f, fa = 2000.0f, t = std::tan(20.0f * 3.14159265f / 180.0f), aspect = float(w) / float(h);
    const M4 proj{1.0f / (aspect * t), 0, 0, 0, 0, 1.0f / t, 0, 0, 0, 0, -(fa + n) / (fa - n), -1, 0, 0, -2 * fa * n / (fa - n), 0};
    TraceCamera c;
    c.invViewProj = invert(mul(proj, view));
    c.width = w;
    c.height = h;
    return c;
}

std::shared_ptr<TraceMesh> box(V3 lo, V3 hi, const BodyMaterial &m) {
    auto mesh = std::make_shared<TraceMesh>();
    auto quad = [&](V3 a, V3 b, V3 c, V3 d, V3 n) {
        const uint32_t i = uint32_t(mesh->positions.size() / 3);
        for(const V3 &v : {a, b, c, d}) {
            mesh->positions.insert(mesh->positions.end(), {v.x, v.y, v.z});
            mesh->normals.insert(mesh->normals.end(), {n.x, n.y, n.z});
        }
        mesh->indices.insert(mesh->indices.end(), {i, i + 1, i + 2, i, i + 2, i + 3});
    };
    const float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
    quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1});
    quad({x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, {x0, y0, z0}, {0, 0, -1});
    quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0});
    quad({x1, y1, z0}, {x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {0, 1, 0});
    quad({x0, y1, z0}, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {-1, 0, 0});
    quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0});
    mesh->optics = opticsFor(m);
    mesh->translucent = m.finish == Finish::Translucent;
    return mesh;
}

BodyMaterial material(PrintMaterial pm, Finish f, uint32_t rgb) {
    BodyMaterial m;
    m.material = pm;
    m.finish = f;
    m.rgb = rgb;
    return m;
}

V3 at(const std::vector<float> &img, const TraceCamera &c, int x, int y) {
    const size_t i = size_t(y) * size_t(c.width) + size_t(x);
    return {img[3 * i], img[3 * i + 1], img[3 * i + 2]};
}

// The average over a small window (less noise).
V3 around(const std::vector<float> &img, const TraceCamera &c, int x, int y, int r = 2) {
    V3 s;
    int n = 0;
    for(int j = -r; j <= r; ++j)
        for(int i = -r; i <= r; ++i) {
            s += at(img, c, x + i, y + j);
            ++n;
        }
    return s / float(n);
}

} // namespace

TEST_CASE("path tracer: sees the body and the plate, and repeats itself") {
    TraceScene scene;
    scene.meshes.push_back(box({-10, -10, 0}, {10, 10, 20}, material(PrintMaterial::PLA, Finish::Matte, 0xc02030)));
    const TraceCamera cam = camera({60, -80, 60}, {0, 0, 10}, 64, 48);
    std::vector<float> cover;
    const std::vector<float> a = PathTracer::renderLinear(scene, cam, 4, 7, &cover);
    // The body at the centre, red; the plate fills the lower edge.
    CHECK(cover[size_t(24 * 64 + 32)] == doctest::Approx(1.0f));
    const V3 c = around(a, cam, 32, 24, 1);
    CHECK(c.x > c.y * 1.5f);
    CHECK(cover[size_t(47 * 64 + 2)] == doctest::Approx(1.0f));
    // Without a plate, the corners see nothing.
    scene.settings.plate = BuildPlateKind::None;
    PathTracer::renderLinear(scene, cam, 1, 7, &cover);
    CHECK(cover[0] == doctest::Approx(0.0f));
    scene.settings.plate = BuildPlateKind::TexturedPEI;
    // The same seed: the same image.
    const std::vector<float> b = PathTracer::renderLinear(scene, cam, 4, 7);
    CHECK(a == b);
    const TraceImage img = PathTracer::renderNow(scene, cam, 2, false);
    CHECK(img.rgba.size() == size_t(64 * 48 * 4));
    CHECK(img.samples == 2);
}

TEST_CASE("path tracer: lit from the key, with a shadow on the plate") {
    for(Lighting l : {Lighting::Studio, Lighting::Daylight}) {
        TraceScene scene;
        scene.settings.lighting = l;
        scene.settings.layerLines = false;
        scene.meshes.push_back(box({-5, -5, 0}, {5, 5, 40}, material(PrintMaterial::PLA, Finish::Matte, 0xe0e0e0)));
        // Straight down onto the plate: the pole's shadow runs away from the key.
        const TraceCamera cam = camera({0, -0.01f, 300}, {0, 0, 0}, 96, 96);
        const std::vector<float> img = PathTracer::renderLinear(scene, cam, 16, 3);
        const Environment env = environment(l);
        // Where the shadow falls: 25 mm from the pole, away from the light.
        auto pixelOf = [&](float x, float y) {
            const float t = std::tan(20.0f * 3.14159265f / 180.0f) * 300.0f; // half the view at the plate
            return std::pair<int, int>{int((x / t * 0.5f + 0.5f) * 96), int((0.5f - y / t * 0.5f) * 96)};
        };
        const V3 kd = normalize(V3(env.key.dir.x, env.key.dir.y, 0));
        const auto [sx, sy] = pixelOf(-kd.x * 25, -kd.y * 25);
        const auto [lx, ly] = pixelOf(kd.x * 25, kd.y * 25);
        const float shadow = luminance(around(img, cam, sx, sy)), lit = luminance(around(img, cam, lx, ly));
        CHECK(lit > 1.5f * shadow);
        // The top of the pole, white plastic facing up, is bright but finite.
        const float top = luminance(around(img, cam, 48, 48, 1));
        CHECK(top > 0.4f);
        CHECK(top < 4.0f);
    }
}

TEST_CASE("path tracer: semitransparent plastic tints and dims what is behind it") {
    auto view = [](float thickness) {
        TraceScene scene;
        scene.settings.plate = BuildPlateKind::None;
        scene.settings.layerLines = false;
        // A white wall, and a blue PETG slab in front of it.
        scene.meshes.push_back(box({-30, 20, 0}, {30, 24, 40}, material(PrintMaterial::PLA, Finish::Matte, 0xf0f0f0)));
        if(thickness > 0)
            scene.meshes.push_back(box({-12, 0, 8}, {12, thickness, 32},
                                       material(PrintMaterial::PETG, Finish::Translucent, 0x6aa8e8)));
        const TraceCamera cam = camera({0, -150, 20}, {0, 0, 20}, 64, 64);
        const std::vector<float> img = PathTracer::renderLinear(scene, cam, 64, 5);
        return around(img, cam, 32, 32, 3);
    };
    const V3 bare = view(0), thin = view(2), thick = view(12);
    // Through the plastic the white wall turns blue, and darker with thickness.
    CHECK(thin.z / std::max(thin.x, 1e-4f) > bare.z / std::max(bare.x, 1e-4f) * 1.15f);
    CHECK(luminance(thick) < luminance(thin));
    CHECK(luminance(thin) < luminance(bare) * 1.05f);
    // But it is still light, not black: the plastic lets light through.
    CHECK(luminance(thin) > 0.15f * luminance(bare));
}

TEST_CASE("path tracer: tone mapping keeps colours and stays in range") {
    const V3 black = toneMapNeutral(V3(0, 0, 0)), white = toneMapNeutral(V3(100, 100, 100));
    CHECK(black.x == doctest::Approx(0.0f).epsilon(0.02));
    CHECK(white.x <= 1.0f);
    CHECK(white.x > 0.95f);
    const V3 red = toneMapNeutral(V3(0.5f, 0.05f, 0.05f));
    CHECK(red.x > 3.0f * red.y);
}
