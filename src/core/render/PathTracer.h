#pragma once

#include "render/Materials.h"
#include "render/RenderMath.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cad::rt {

// A body for the path tracer: triangles (mm, world) with vertex normals, and
// what it is printed in.
struct TraceMesh {
    std::vector<float> positions; // x y z per vertex
    std::vector<float> normals;   // per vertex
    std::vector<uint32_t> indices;
    Optics optics;
    bool translucent = false;
};

struct TraceScene {
    std::vector<std::shared_ptr<const TraceMesh>> meshes;
    RenderSettings settings;
};

// The camera as the canvas has it: the inverse of its view-projection
// (column-major, OpenGL clip space: z from -1 at the near plane to 1 at the
// far one), and the image size. Image row 0 is the top.
struct TraceCamera {
    std::array<float, 16> invViewProj{};
    int width = 0, height = 0;
    bool operator==(const TraceCamera &o) const {
        return invViewProj == o.invViewProj && width == o.width && height == o.height;
    }
};

// What a render gives: tone-mapped sRGB with premultiplied alpha (alpha is
// where the rays met the scene; the canvas's backdrop shows elsewhere).
struct TraceImage {
    int width = 0, height = 0;
    int samples = 0;
    bool denoised = false;
    std::vector<uint8_t> rgba;
};

// A progressive, physically based path tracer (Embree): the build plate, the
// studio or daylight environment with its key light (next-event estimation
// with multiple importance sampling), rough plastic with the print's layer
// lines, silk's anisotropic sheen, and semitransparent plastic as a rough
// dielectric with light scattering and absorbed inside (a random walk).
// Images refine in the background; any change restarts them. With Open Image
// Denoise, images are denoised at 4, 8, 16... samples.
class PathTracer {
public:
    PathTracer();
    ~PathTracer();
    PathTracer(const PathTracer &) = delete;
    PathTracer &operator=(const PathTracer &) = delete;

    // Restart with this scene / camera (in the background).
    void render(TraceScene scene, const TraceCamera &camera, int maxSamples);
    void stop();
    bool running() const;

    // The newest image, if newer than `generation` (which is updated).
    bool takeImage(TraceImage &out, uint64_t &generation) const;

    // A whole render, in this thread (tests, saving images).
    static TraceImage renderNow(const TraceScene &scene, const TraceCamera &camera, int samples, bool denoise,
                                uint32_t seed = 1);
    // Linear radiance (RGB per pixel) and coverage, for tests.
    static std::vector<float> renderLinear(const TraceScene &scene, const TraceCamera &camera, int samples,
                                           uint32_t seed, std::vector<float> *coverage = nullptr);

    static bool denoiserAvailable();

    struct Impl;

private:
    void loop();

    std::unique_ptr<Impl> m;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_quit = false;
    bool m_pending = false;
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_running{false};
    TraceScene m_nextScene;
    TraceCamera m_nextCamera;
    int m_nextMax = 64;
    TraceImage m_image;
    uint64_t m_imageGeneration = 0;
};

// Khronos PBR Neutral tone mapping and sRGB encoding (as the live preview's shaders).
V3 toneMapNeutral(V3 c);

} // namespace cad::rt
