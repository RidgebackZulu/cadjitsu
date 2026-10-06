#include "render/PathTracer.h"

#include "render/BuildPlate.h"
#include "render/Environment.h"
#include "render/PrintSurface.h"

#include <embree4/rtcore.h>
#ifdef CADJITSU_HAVE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

#include <chrono>
#include <cmath>
#include <cstring>

namespace cad::rt {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kEps = 2e-3f; // ray offset, mm

// --- random numbers ---------------------------------------------------------------

struct Rng {
    uint64_t state, inc;
    Rng(uint64_t seed, uint64_t seq) : state(0), inc((seq << 1u) | 1u) {
        next();
        state += seed;
        next();
    }
    uint32_t next() {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = uint32_t(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }
    float uniform() { return float(next() >> 8) * (1.0f / 16777216.0f); }
};

float luminance(const V3 &c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
V3 expV(const V3 &v) { return {std::exp(v.x), std::exp(v.y), std::exp(v.z)}; }
V3 reflectV(const V3 &v, const V3 &n) { return v - n * (2.0f * dot(v, n)); }

V3 schlick(const V3 &f0, float c) {
    const float m = std::pow(1.0f - clampf(c, 0.0f, 1.0f), 5.0f);
    return f0 + (V3(1, 1, 1) - f0) * m;
}

// Exact Fresnel reflectance of a dielectric; eta = n_incident / n_transmitted.
float fresnelDielectric(float cosI, float eta) {
    cosI = clampf(cosI, 0.0f, 1.0f);
    const float sin2T = eta * eta * (1.0f - cosI * cosI);
    if(sin2T >= 1.0f) return 1.0f;
    const float cosT = std::sqrt(1.0f - sin2T);
    const float rs = (eta * cosI - cosT) / (eta * cosI + cosT);
    const float rp = (cosI - eta * cosT) / (cosI + eta * cosT);
    return 0.5f * (rs * rs + rp * rp);
}

// --- GGX (local frame: x = tangent, y = bitangent, z = normal) --------------------

float ggxD(const V3 &h, float ax, float ay) {
    const float d = h.x * h.x / (ax * ax) + h.y * h.y / (ay * ay) + h.z * h.z;
    return 1.0f / (kPi * ax * ay * d * d);
}
float ggxLambda(const V3 &v, float ax, float ay) {
    const float z2 = v.z * v.z;
    if(z2 <= 0.0f) return 1e6f;
    return 0.5f * (-1.0f + std::sqrt(1.0f + (ax * ax * v.x * v.x + ay * ay * v.y * v.y) / z2));
}
float ggxG1(const V3 &v, float ax, float ay) { return 1.0f / (1.0f + ggxLambda(v, ax, ay)); }

// Visible-normal sampling (Heitz 2018).
V3 sampleVndf(const V3 &ve, float ax, float ay, float u1, float u2) {
    const V3 vh = normalize(V3(ax * ve.x, ay * ve.y, ve.z));
    const float lensq = vh.x * vh.x + vh.y * vh.y;
    const V3 t1 = lensq > 0 ? V3(-vh.y, vh.x, 0) / std::sqrt(lensq) : V3(1, 0, 0);
    const V3 t2 = cross(vh, t1);
    const float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
    const float p1 = r * std::cos(phi);
    float p2 = r * std::sin(phi);
    const float s = 0.5f * (1.0f + vh.z);
    p2 = (1.0f - s) * std::sqrt(std::max(0.0f, 1.0f - p1 * p1)) + s * p2;
    const V3 nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0f, 1.0f - p1 * p1 - p2 * p2));
    return normalize(V3(ax * nh.x, ay * nh.y, std::max(1e-6f, nh.z)));
}

V3 cosineHemisphere(float u1, float u2) {
    const float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
    return {r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u1))};
}

// Henyey-Greenstein phase function: a new direction around `dir`.
V3 sampleHG(const V3 &dir, float g, float u1, float u2) {
    float cosT;
    if(std::abs(g) < 1e-3f) cosT = 1.0f - 2.0f * u1;
    else {
        const float sq = (1.0f - g * g) / (1.0f - g + 2.0f * g * u1);
        cosT = (1.0f + g * g - sq * sq) / (2.0f * g);
    }
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT)), phi = 2.0f * kPi * u2;
    V3 t, b;
    basis(dir, t, b);
    return normalize(t * (sinT * std::cos(phi)) + b * (sinT * std::sin(phi)) + dir * cosT);
}

// --- materials ------------------------------------------------------------------------

struct Frame {
    V3 t, b, n;
    V3 toLocal(const V3 &v) const { return {dot(v, t), dot(v, b), dot(v, n)}; }
    V3 toWorld(const V3 &v) const { return t * v.x + b * v.y + n * v.z; }
};

// Plastic: a diffuse body under a GGX specular (anisotropic for silk),
// optionally under a clear coat (smooth PEI).
struct Plastic {
    V3 diffuse, f0;
    float ax = 0.25f, ay = 0.25f;
    float sheen = 0.0f;
    bool coat = false;
    float pSpec = 0.5f, pCoat = 0.0f;

    void prepare(float nov) {
        const float fs = luminance(schlick(f0, nov));
        const float fd = luminance(diffuse) * (1.0f - fs);
        pSpec = clampf(fs / std::max(fs + fd, 1e-4f), 0.1f, 0.9f);
        pCoat = coat ? clampf(2.0f * (0.04f + 0.96f * std::pow(1.0f - nov, 5.0f)), 0.05f, 0.5f) : 0.0f;
    }
    V3 base(const V3 &v, const V3 &l, float &pdf) const {
        pdf = 0.0f;
        if(v.z <= 0.0f || l.z <= 0.0f) return {};
        const V3 h = normalize(v + l);
        const V3 F = schlick(f0, dot(v, h));
        const float G2 = 1.0f / (1.0f + ggxLambda(v, ax, ay) + ggxLambda(l, ax, ay));
        const float D = ggxD(h, ax, ay);
        const V3 spec = F * (D * G2 / (4.0f * v.z * l.z));
        const float pdfSpec = ggxG1(v, ax, ay) * D / (4.0f * v.z);
        const float sheenBoost = 1.0f + sheen * 0.6f * std::pow(1.0f - v.z, 3.0f);
        const V3 diff = diffuse * ((1.0f - maxc(schlick(f0, v.z))) * sheenBoost / kPi);
        pdf = pSpec * pdfSpec + (1.0f - pSpec) * l.z / kPi;
        return spec + diff;
    }
    V3 eval(const V3 &v, const V3 &l, float &pdf) const {
        V3 f = base(v, l, pdf);
        if(!coat) return f;
        pdf *= 1.0f - pCoat;
        if(v.z <= 0.0f || l.z <= 0.0f) return {};
        const float ca = 0.02f;
        const V3 h = normalize(v + l);
        const float Fc = 0.04f + 0.96f * std::pow(1.0f - dot(v, h), 5.0f);
        const float D = ggxD(h, ca, ca);
        const float G2 = 1.0f / (1.0f + ggxLambda(v, ca, ca) + ggxLambda(l, ca, ca));
        const float cv = 0.04f + 0.96f * std::pow(1.0f - v.z, 5.0f), cl = 0.04f + 0.96f * std::pow(1.0f - l.z, 5.0f);
        pdf += pCoat * ggxG1(v, ca, ca) * D / (4.0f * v.z);
        return f * ((1.0f - cv) * (1.0f - cl)) + V3(1, 1, 1) * (Fc * D * G2 / (4.0f * v.z * l.z));
    }
    // A new direction; false if none.
    bool sample(const V3 &v, Rng &rng, V3 &l) const {
        const float u = rng.uniform();
        if(coat && u < pCoat) {
            const V3 h = sampleVndf(v, 0.02f, 0.02f, rng.uniform(), rng.uniform());
            l = reflectV(-v, h);
        } else if(rng.uniform() < pSpec) {
            const V3 h = sampleVndf(v, ax, ay, rng.uniform(), rng.uniform());
            l = reflectV(-v, h);
        } else {
            l = cosineHemisphere(rng.uniform(), rng.uniform());
        }
        return l.z > 0.0f;
    }
};

V3 neutral(V3 c) {
    const float startCompression = 0.8f - 0.04f, desaturation = 0.15f;
    const float x = std::min(c.x, std::min(c.y, c.z));
    const float offset = x < 0.08f ? x - 6.25f * x * x : 0.04f;
    c = c - V3(offset, offset, offset);
    const float peak = maxc(c);
    if(peak >= startCompression) {
        const float d = 1.0f - startCompression;
        const float newPeak = 1.0f - d * d / (peak + d - startCompression);
        c = c * (newPeak / peak);
        const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
        c = mix(c, V3(newPeak, newPeak, newPeak), g);
    }
    return c;
}

} // namespace

V3 toneMapNeutral(V3 c) {
    c = neutral(V3(std::max(c.x, 0.0f), std::max(c.y, 0.0f), std::max(c.z, 0.0f)));
    auto enc = [](float v) {
        v = clampf(v, 0.0f, 1.0f);
        return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    };
    return {enc(c.x), enc(c.y), enc(c.z)};
}

// --- the scene and the integrator ----------------------------------------------------

struct PathTracer::Impl {
    struct Geo {
        const TraceMesh *mesh = nullptr; // a body, or the plate
        const std::vector<int> *parts = nullptr;
    };

    RTCDevice device = nullptr;
    RTCScene scene = nullptr;
    TraceScene src;
    std::vector<Geo> geos;
    PlateMesh plate;
    TraceMesh plateTrace;
    PlateLayout layout;
    Environment env;
    float cosKey = 1.0f, keyPdf = 1.0f;
    SurfaceParams surface;
    TraceCamera cam;
    float spreadConst = 0.0f, spreadAngle = 0.0f;

    // Accumulated per pixel: radiance (rgb), hits, and the first hit's albedo and normal.
    std::vector<float> sum, hits, albedo, normal;
    int samples = 0;

    Impl() { device = rtcNewDevice(nullptr); }
    ~Impl() {
        if(scene) rtcReleaseScene(scene);
        if(device) rtcReleaseDevice(device);
    }

    void addGeometry(const float *pos, size_t nv, const uint32_t *idx, size_t nt) {
        RTCGeometry g = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);
        auto *v = static_cast<float *>(rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 3 * sizeof(float), nv));
        std::memcpy(v, pos, nv * 3 * sizeof(float));
        auto *ix = static_cast<uint32_t *>(rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(uint32_t), nt));
        std::memcpy(ix, idx, nt * 3 * sizeof(uint32_t));
        rtcCommitGeometry(g);
        rtcAttachGeometry(scene, g);
        rtcReleaseGeometry(g);
    }

    void build(const TraceScene &s, const TraceCamera &c) {
        src = s;
        cam = c;
        if(scene) rtcReleaseScene(scene);
        scene = rtcNewScene(device);
        geos.clear();
        V3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
        for(const auto &m : src.meshes) {
            if(!m || m->indices.empty()) continue;
            for(size_t i = 0; i + 2 < m->positions.size(); i += 3) {
                lo = V3(std::min(lo.x, m->positions[i]), std::min(lo.y, m->positions[i + 1]), std::min(lo.z, m->positions[i + 2]));
                hi = V3(std::max(hi.x, m->positions[i]), std::max(hi.y, m->positions[i + 1]), std::max(hi.z, m->positions[i + 2]));
            }
            addGeometry(m->positions.data(), m->positions.size() / 3, m->indices.data(), m->indices.size() / 3);
            geos.push_back({m.get(), nullptr});
        }
        if(geos.empty()) lo = hi = V3();
        layout = plateLayout(V3(lo.x, lo.y, lo.z - 0.02f), hi, src.settings);
        if(layout.present) {
            plate = plateMesh(layout);
            plateTrace.positions.clear();
            plateTrace.normals.clear();
            for(size_t i = 0; i < plate.positions.size(); ++i) {
                plateTrace.positions.insert(plateTrace.positions.end(), {plate.positions[i].x, plate.positions[i].y, plate.positions[i].z});
                plateTrace.normals.insert(plateTrace.normals.end(), {plate.normals[i].x, plate.normals[i].y, plate.normals[i].z});
            }
            plateTrace.indices = plate.indices;
            addGeometry(plateTrace.positions.data(), plate.positions.size(), plate.indices.data(), plate.indices.size() / 3);
            geos.push_back({&plateTrace, &plate.part});
        }
        rtcCommitScene(scene);

        env = environment(src.settings.lighting);
        cosKey = std::cos(env.key.angularRadius);
        keyPdf = 1.0f / (2.0f * kPi * (1.0f - cosKey));
        surface.layerHeight = float(src.settings.layerHeight);
        surface.lineWidth = float(src.settings.lineWidth);
        surface.plateZ = lo.z;
        surface.plateKind = int(src.settings.plate);

        // How wide a pixel is: a constant (orthographic) plus an angle (perspective).
        V3 o0, d0, o1, d1;
        primary(0.5f * float(cam.width), 0.5f * float(cam.height), o0, d0);
        primary(0.5f * float(cam.width) + 1.0f, 0.5f * float(cam.height), o1, d1);
        spreadConst = length(o1 - o0);
        spreadAngle = length(d1 - d0);

        const size_t n = size_t(cam.width) * size_t(cam.height);
        sum.assign(n * 3, 0.0f);
        hits.assign(n, 0.0f);
        albedo.assign(n * 3, 0.0f);
        normal.assign(n * 3, 0.0f);
        samples = 0;
    }

    V3 unproject(float nx, float ny, float nz) const {
        const float *m = cam.invViewProj.data();
        const float x = m[0] * nx + m[4] * ny + m[8] * nz + m[12];
        const float y = m[1] * nx + m[5] * ny + m[9] * nz + m[13];
        const float z = m[2] * nx + m[6] * ny + m[10] * nz + m[14];
        const float w = m[3] * nx + m[7] * ny + m[11] * nz + m[15];
        return V3(x, y, z) / w;
    }

    void primary(float px, float py, V3 &org, V3 &dir) const {
        const float nx = 2.0f * px / float(cam.width) - 1.0f, ny = 1.0f - 2.0f * py / float(cam.height);
        const V3 a = unproject(nx, ny, -1.0f), b = unproject(nx, ny, 1.0f);
        org = a;
        dir = normalize(b - a);
    }

    struct Hit {
        bool hit = false;
        float t = 0;
        int geo = -1, prim = -1;
        V3 p, ng, ns; // ns: interpolated normal (outward); ng: geometric, on the side of ns
    };

    Hit intersect(const V3 &o, const V3 &d, float tfar = 1e30f) const {
        RTCRayHit rh;
        rh.ray.org_x = o.x, rh.ray.org_y = o.y, rh.ray.org_z = o.z;
        rh.ray.dir_x = d.x, rh.ray.dir_y = d.y, rh.ray.dir_z = d.z;
        rh.ray.tnear = 0.0f;
        rh.ray.tfar = tfar;
        rh.ray.mask = 0xffffffffu;
        rh.ray.flags = 0;
        rh.ray.time = 0;
        rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
        rh.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
        rtcIntersect1(scene, &rh);
        Hit h;
        if(rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return h;
        h.hit = true;
        h.t = rh.ray.tfar;
        h.geo = int(rh.hit.geomID);
        h.prim = int(rh.hit.primID);
        h.p = o + d * h.t;
        const TraceMesh &m = *geos[size_t(h.geo)].mesh;
        const uint32_t *ix = &m.indices[size_t(h.prim) * 3];
        const float u = rh.hit.u, v = rh.hit.v, w = 1.0f - u - v;
        auto nrm = [&](uint32_t i) { return V3(m.normals[3 * i], m.normals[3 * i + 1], m.normals[3 * i + 2]); };
        h.ns = normalize(nrm(ix[0]) * w + nrm(ix[1]) * u + nrm(ix[2]) * v);
        h.ng = normalize(V3(rh.hit.Ng_x, rh.hit.Ng_y, rh.hit.Ng_z));
        if(dot(h.ng, h.ns) < 0.0f) h.ng = -h.ng;
        return h;
    }

    bool isTranslucent(int geo) const {
        const TraceMesh *m = geos[size_t(geo)].mesh;
        return !geos[size_t(geo)].parts && m->translucent;
    }

    // Light reaching p from direction d (towards the key): 0 behind opaque
    // things, tinted and dimmed through semitransparent bodies.
    V3 transmittance(V3 p, const V3 &d) const {
        V3 T(1, 1, 1);
        float enteredAt = -1.0f;
        int enteredGeo = -1;
        float travelled = 0.0f;
        for(int i = 0; i < 24; ++i) {
            const Hit h = intersect(p, d);
            if(!h.hit) return T;
            if(!isTranslucent(h.geo)) return {};
            const Optics &o = geos[size_t(h.geo)].mesh->optics;
            const float f0 = std::pow((o.ior - 1.0f) / (o.ior + 1.0f), 2.0f);
            T *= std::sqrt(o.transmission) * (1.0f - f0);
            const float at = travelled + h.t;
            if(dot(h.ns, d) < 0.0f) {
                enteredAt = at;
                enteredGeo = h.geo;
            } else if(enteredGeo == h.geo && enteredAt >= 0.0f) {
                const float len = plasticAlong(at - enteredAt, surface.lineWidth, float(src.settings.infill));
                const float s = (1.0f - o.phaseG) / std::max(o.scatterMm, 0.05f);
                T *= expV(V3(o.absorbPerMm[0] + s, o.absorbPerMm[1] + s, o.absorbPerMm[2] + s) * -len);
                enteredGeo = -1;
            }
            if(maxc(T) < 1e-4f) return {};
            travelled = at + kEps;
            p = h.p + d * kEps;
        }
        return T;
    }

    V3 sampleKey(Rng &rng) const {
        const float cosT = 1.0f - rng.uniform() * (1.0f - cosKey);
        const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT)), phi = 2.0f * kPi * rng.uniform();
        V3 t, b;
        basis(env.key.dir, t, b);
        return normalize(t * (sinT * std::cos(phi)) + b * (sinT * std::sin(phi)) + env.key.dir * cosT);
    }

    V3 escaped(const V3 &d, float bsdfPdf, bool mis, bool skipKey = false) const {
        V3 L = env.radiance(d);
        if(!skipKey && dot(d, env.key.dir) >= cosKey) {
            float w = 1.0f;
            if(mis) w = bsdfPdf * bsdfPdf / (bsdfPdf * bsdfPdf + keyPdf * keyPdf);
            L += env.key.radiance * w;
        }
        return L;
    }

    // One path from the camera.
    V3 trace(V3 o, V3 d, Rng &rng, float footprint0, V3 *aovAlbedo, V3 *aovNormal, bool &primaryHit) {
        V3 L, beta(1, 1, 1);
        float lastPdf = 0.0f;
        bool lastMis = false; // the previous vertex sampled the key light
        const Optics *medium = nullptr;
        float density = 1.0f; // plastic per mm of the path inside (walls and infill)
        bool skipKey = false;  // the key light was sampled directly from inside a body
        int scatterEvents = 0;
        float travelled = 0.0f;
        primaryHit = false;
        for(int depth = 0; depth < 24; ++depth) {
            const Hit h = intersect(o, d);
            // Inside a semitransparent body: scattering on the way.
            if(medium) {
                const float sigmaS = density / std::max(medium->scatterMm, 0.05f);
                const float s = -std::log(std::max(1.0f - rng.uniform(), 1e-7f)) / sigmaS;
                const float tmax = h.hit ? h.t : 1e3f;
                const float t = std::min(s, tmax);
                beta *= expV(V3(medium->absorbPerMm[0], medium->absorbPerMm[1], medium->absorbPerMm[2]) * (-t * density));
                if(s < tmax) {
                    o = o + d * s;
                    // The key light, directly: through the rest of the body and out.
                    const V3 ld = sampleKey(rng);
                    const Hit out = intersect(o, ld);
                    if(out.hit && isTranslucent(out.geo) && geos[size_t(out.geo)].mesh->translucent &&
                       &geos[size_t(out.geo)].mesh->optics == medium) {
                        const float g = medium->phaseG, c = dot(d, ld);
                        const float phase = (1.0f - g * g) / (4.0f * kPi * std::pow(std::max(1.0f + g * g - 2.0f * g * c, 1e-4f), 1.5f));
                        const float sOut = (1.0f - g) / std::max(medium->scatterMm, 0.05f);
                        const V3 inside = expV(V3(medium->absorbPerMm[0] + sOut, medium->absorbPerMm[1] + sOut,
                                                  medium->absorbPerMm[2] + sOut) * (-out.t * density));
                        const float F = fresnelDielectric(std::abs(dot(out.ns, ld)), medium->ior);
                        const V3 T = transmittance(out.p + ld * kEps, ld);
                        L += beta * inside * T * env.key.radiance * ((1.0f - F) * phase / keyPdf);
                    }
                    skipKey = true;
                    d = sampleHG(d, medium->phaseG, rng.uniform(), rng.uniform());
                    lastMis = false;
                    if(++scatterEvents > 256) break;
                    --depth;
                    continue;
                }
            }
            if(!h.hit) {
                L += beta * escaped(d, lastPdf, lastMis, skipKey);
                break;
            }
            travelled += h.t;
            const Geo &g = geos[size_t(h.geo)];
            const bool isPlate = g.parts != nullptr;
            const bool translucent = !isPlate && g.mesh->translucent;
            const V3 wo = -d;
            const bool outside = dot(h.ns, wo) > 0.0f;
            V3 ng = outside ? h.ng : -h.ng;
            V3 ns = outside ? h.ns : -h.ns;
            const float footprint = footprint0 * (1.0f + float(depth)) + spreadAngle * travelled;

            // The surface: the plate's look, or the print's.
            Frame fr;
            Plastic mat;
            const Optics *optics = nullptr;
            if(isPlate) {
                const int part = (*g.parts)[size_t(h.prim)];
                const PlateShade sh = plateShade(layout, part, h.p, ng);
                fr.n = normalize(sh.normal);
                basis(fr.n, fr.t, fr.b);
                mat.diffuse = sh.albedo * (1.0f - sh.metalness);
                mat.f0 = mix(V3(0.04f, 0.04f, 0.04f), sh.albedo, sh.metalness);
                mat.ax = mat.ay = std::max(sh.roughness * sh.roughness, 0.002f);
                mat.coat = part == 0 && layout.kind == BuildPlateKind::SmoothPEI;
            } else {
                optics = &g.mesh->optics;
                SurfaceParams sp = surface;
                sp.strength = src.settings.layerLines ? optics->layerStrength : 0.0f;
                sp.micro = optics->microRoughness;
                sp.footprint = footprint;
                const SurfaceSample ss = printSurface(h.p, outside ? h.ns : -h.ns, sp);
                fr.n = ss.normal;
                fr.t = normalize(ss.tangent - fr.n * dot(ss.tangent, fr.n));
                fr.b = cross(fr.n, fr.t);
                const float f0d = std::pow((optics->ior - 1.0f) / (optics->ior + 1.0f), 2.0f);
                const V3 alb(optics->albedo[0], optics->albedo[1], optics->albedo[2]);
                const V3 tint(optics->specTint[0], optics->specTint[1], optics->specTint[2]);
                mat.diffuse = alb * (1.0f - optics->metalness) * ss.cavity;
                mat.f0 = mix(V3(f0d, f0d, f0d), tint, optics->metalness);
                const float ra = clampf(optics->roughness + ss.roughnessAdd, 0.03f, 1.0f);
                const float rc = clampf(optics->roughnessAcross + ss.roughnessAdd + ss.acrossAdd, 0.03f, 1.0f);
                mat.ax = std::max(ra * ra, 0.002f);
                mat.ay = std::max(rc * rc, 0.002f);
                mat.sheen = optics->sheen;
            }
            V3 wl = fr.toLocal(wo);
            if(wl.z <= 1e-4f) {
                // The bumped normal faces away: fall back to the true one.
                fr.n = ns;
                basis(fr.n, fr.t, fr.b);
                wl = fr.toLocal(wo);
            }
            if(depth == 0) {
                primaryHit = true;
                if(aovAlbedo) *aovAlbedo = translucent ? V3(optics->albedo[0], optics->albedo[1], optics->albedo[2]) : mat.diffuse + mat.f0;
                if(aovNormal) *aovNormal = fr.n;
            }

            // Semitransparent: a rough dielectric boundary (its opaque share
            // is the plastic below).
            if(translucent && (!outside || rng.uniform() < optics->transmission)) {
                const float a = std::max(optics->roughness * optics->roughness, 0.002f);
                const float eta = outside ? 1.0f / optics->ior : optics->ior;
                Frame df;
                df.n = outside ? h.ns : -h.ns;
                basis(df.n, df.t, df.b);
                const V3 v = df.toLocal(wo);
                if(v.z <= 0.0f) break;
                const V3 m = sampleVndf(v, a, a, rng.uniform(), rng.uniform());
                const float vom = dot(v, m);
                const float F = fresnelDielectric(vom, eta);
                V3 l;
                if(rng.uniform() < F) {
                    l = reflectV(-v, m);
                    if(l.z <= 0.0f) break;
                    beta *= ggxG1(l, a, a);
                    o = h.p + ng * kEps;
                } else {
                    const float sin2T = eta * eta * (1.0f - vom * vom);
                    const float cosT = std::sqrt(std::max(0.0f, 1.0f - sin2T));
                    l = m * (eta * vom - cosT) - v * eta;
                    if(l.z >= 0.0f) break;
                    beta *= ggxG1(V3(l.x, l.y, -l.z), a, a);
                    o = h.p - ng * kEps;
                    medium = outside ? optics : nullptr;
                    if(medium) {
                        // How much of the way across is plastic (the part's walls and infill).
                        const V3 dw = normalize(df.toWorld(l));
                        const Hit across = intersect(o, dw);
                        const float chord = across.hit ? across.t : 1.0f;
                        density = plasticAlong(chord, surface.lineWidth, float(src.settings.infill)) / std::max(chord, 1e-3f);
                    }
                }
                d = normalize(df.toWorld(l));
                lastMis = false;
                lastPdf = 0.0f;
            } else {
                if(!outside && !translucent) {
                    // Inside an opaque body (a gap in the mesh): stop.
                    break;
                }
                mat.prepare(wl.z);
                // The key light, directly.
                const V3 ld = sampleKey(rng);
                const V3 ll = fr.toLocal(ld);
                if(ll.z > 0.0f && dot(ld, ng) > 0.0f) {
                    float pdf;
                    const V3 f = mat.eval(wl, ll, pdf);
                    if(maxc(f) > 0.0f) {
                        const V3 T = transmittance(h.p + ng * kEps, ld);
                        if(maxc(T) > 0.0f) {
                            const float w = keyPdf * keyPdf / (keyPdf * keyPdf + pdf * pdf);
                            L += beta * f * T * env.key.radiance * (ll.z * w / keyPdf);
                        }
                    }
                }
                V3 l;
                if(!mat.sample(wl, rng, l)) break;
                float pdf;
                const V3 f = mat.eval(wl, l, pdf);
                if(pdf <= 0.0f || maxc(f) <= 0.0f) break;
                const V3 lw = fr.toWorld(l);
                if(dot(lw, ng) <= 0.0f) break;
                beta *= f * (l.z / pdf);
                d = lw;
                o = h.p + ng * kEps;
                lastPdf = pdf;
                lastMis = true;
                skipKey = false;
            }
            // Russian roulette.
            if(depth >= 3) {
                const float q = std::min(0.95f, maxc(beta));
                if(rng.uniform() > q) break;
                beta = beta / q;
            }
        }
        // Clamp rare fireflies (bright paths found by chance).
        const float m = maxc(L);
        if(m > 40.0f) L = L * (40.0f / m);
        return L;
    }

    // One sample per pixel over the whole image, on every core.
    bool pass(uint32_t seed, const std::atomic<bool> *abort) {
        const int W = cam.width, H = cam.height;
        std::atomic<int> nextRow{0};
        std::atomic<bool> aborted{false};
        auto work = [&]() {
            for(int y = nextRow++; y < H; y = nextRow++) {
                if(abort && abort->load()) {
                    aborted = true;
                    return;
                }
                for(int x = 0; x < W; ++x) {
                    const size_t i = size_t(y) * size_t(W) + size_t(x);
                    Rng rng(uint64_t(seed) * 0x9E3779B97F4A7C15ULL + uint64_t(samples), uint64_t(i));
                    V3 o, d;
                    primary(float(x) + rng.uniform(), float(y) + rng.uniform(), o, d);
                    V3 a, n;
                    bool hit = false;
                    const V3 c = trace(o, d, rng, spreadConst, &a, &n, hit);
                    if(hit) {
                        sum[3 * i] += c.x, sum[3 * i + 1] += c.y, sum[3 * i + 2] += c.z;
                        hits[i] += 1.0f;
                        albedo[3 * i] += a.x, albedo[3 * i + 1] += a.y, albedo[3 * i + 2] += a.z;
                        normal[3 * i] += n.x, normal[3 * i + 1] += n.y, normal[3 * i + 2] += n.z;
                    }
                }
            }
        };
        const unsigned n = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> pool;
        for(unsigned k = 1; k < n; ++k) pool.emplace_back(work);
        work();
        for(auto &t : pool) t.join();
        if(aborted) return false;
        ++samples;
        return true;
    }

    TraceImage image(bool denoise) const {
        TraceImage img;
        img.width = cam.width;
        img.height = cam.height;
        img.samples = samples;
        const size_t n = size_t(cam.width) * size_t(cam.height);
        std::vector<float> color(n * 3, 0.0f);
        for(size_t i = 0; i < n; ++i)
            if(hits[i] > 0.0f)
                for(int c = 0; c < 3; ++c) color[3 * i + c] = sum[3 * i + c] / hits[i];
#ifdef CADJITSU_HAVE_OIDN
        if(denoise && samples > 0) {
            std::vector<float> alb(n * 3, 0.0f), nrm(n * 3, 0.0f);
            for(size_t i = 0; i < n; ++i)
                if(hits[i] > 0.0f)
                    for(int c = 0; c < 3; ++c) {
                        alb[3 * i + c] = clampf(albedo[3 * i + c] / hits[i], 0.0f, 1.0f);
                        nrm[3 * i + c] = normal[3 * i + c] / hits[i];
                    }
            oidn::DeviceRef dev = oidn::newDevice(oidn::DeviceType::CPU);
            dev.commit();
            const size_t bytes = n * 3 * sizeof(float);
            oidn::BufferRef cb = dev.newBuffer(bytes), ab = dev.newBuffer(bytes), nb = dev.newBuffer(bytes), ob = dev.newBuffer(bytes);
            cb.write(0, bytes, color.data());
            ab.write(0, bytes, alb.data());
            nb.write(0, bytes, nrm.data());
            oidn::FilterRef f = dev.newFilter("RT");
            f.setImage("color", cb, oidn::Format::Float3, size_t(cam.width), size_t(cam.height));
            f.setImage("albedo", ab, oidn::Format::Float3, size_t(cam.width), size_t(cam.height));
            f.setImage("normal", nb, oidn::Format::Float3, size_t(cam.width), size_t(cam.height));
            f.setImage("output", ob, oidn::Format::Float3, size_t(cam.width), size_t(cam.height));
            f.set("hdr", true);
            f.commit();
            f.execute();
            const char *msg = nullptr;
            if(dev.getError(msg) == oidn::Error::None) {
                ob.read(0, bytes, color.data());
                img.denoised = true;
            }
        }
#else
        (void)denoise;
#endif
        img.rgba.resize(n * 4);
        for(size_t i = 0; i < n; ++i) {
            const float a = samples > 0 ? clampf(hits[i] / float(samples), 0.0f, 1.0f) : 0.0f;
            const V3 c = toneMapNeutral(V3(color[3 * i], color[3 * i + 1], color[3 * i + 2]));
            img.rgba[4 * i] = uint8_t(std::lround(c.x * a * 255.0f));
            img.rgba[4 * i + 1] = uint8_t(std::lround(c.y * a * 255.0f));
            img.rgba[4 * i + 2] = uint8_t(std::lround(c.z * a * 255.0f));
            img.rgba[4 * i + 3] = uint8_t(std::lround(a * 255.0f));
        }
        return img;
    }
};

// --- the background renderer -------------------------------------------------------------

PathTracer::PathTracer() : m(std::make_unique<Impl>()) { m_thread = std::thread([this] { loop(); }); }

PathTracer::~PathTracer() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_quit = true;
        m_abort = true;
    }
    m_cv.notify_all();
    if(m_thread.joinable()) m_thread.join();
}

void PathTracer::render(TraceScene scene, const TraceCamera &camera, int maxSamples) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_nextScene = std::move(scene);
        m_nextCamera = camera;
        m_nextMax = std::max(1, maxSamples);
        m_pending = true;
        m_abort = true;
    }
    m_cv.notify_all();
}

void PathTracer::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending = false;
    m_abort = true;
}

bool PathTracer::running() const { return m_running; }

bool PathTracer::takeImage(TraceImage &out, uint64_t &generation) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_imageGeneration == generation || m_image.rgba.empty()) return false;
    out = m_image;
    generation = m_imageGeneration;
    return true;
}

void PathTracer::loop() {
    for(;;) {
        TraceScene scene;
        TraceCamera camera;
        int maxSamples;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&] { return m_quit || m_pending; });
            if(m_quit) return;
            scene = std::move(m_nextScene);
            camera = m_nextCamera;
            maxSamples = m_nextMax;
            m_pending = false;
            m_abort = false;
        }
        if(camera.width <= 0 || camera.height <= 0) continue;
        m_running = true;
        m->build(scene, camera);
        auto lastShown = std::chrono::steady_clock::now();
        while(m->samples < maxSamples) {
            if(!m->pass(1, &m_abort)) break;
            const int s = m->samples;
            const bool power = (s & (s - 1)) == 0;
            const auto now = std::chrono::steady_clock::now();
            // Noisy images while there are few samples, then (denoised) ones at
            // each doubling, and the last.
            const bool denoise = denoiserAvailable() && s >= 4;
            const bool publish = s < 4 || power || s == maxSamples ||
                                 (!denoise && now - lastShown > std::chrono::seconds(2));
            if(publish) {
                TraceImage img = m->image(denoise);
                std::lock_guard<std::mutex> lock(m_mutex);
                if(m_abort) break;
                m_image = std::move(img);
                ++m_imageGeneration;
                lastShown = now;
            }
            if(m_abort) break;
        }
        m_running = false;
    }
}

TraceImage PathTracer::renderNow(const TraceScene &scene, const TraceCamera &camera, int samples, bool denoise,
                                 uint32_t seed) {
    Impl impl;
    impl.build(scene, camera);
    for(int i = 0; i < samples; ++i) impl.pass(seed, nullptr);
    return impl.image(denoise);
}

std::vector<float> PathTracer::renderLinear(const TraceScene &scene, const TraceCamera &camera, int samples,
                                            uint32_t seed, std::vector<float> *coverage) {
    Impl impl;
    impl.build(scene, camera);
    for(int i = 0; i < samples; ++i) impl.pass(seed, nullptr);
    const size_t n = size_t(camera.width) * size_t(camera.height);
    std::vector<float> out(n * 3, 0.0f);
    if(coverage) coverage->assign(n, 0.0f);
    for(size_t i = 0; i < n; ++i) {
        if(impl.hits[i] > 0.0f)
            for(int c = 0; c < 3; ++c) out[3 * i + c] = impl.sum[3 * i + c] / impl.hits[i];
        if(coverage) (*coverage)[i] = impl.hits[i] / float(std::max(samples, 1));
    }
    return out;
}

bool PathTracer::denoiserAvailable() {
#ifdef CADJITSU_HAVE_OIDN
    return true;
#else
    return false;
#endif
}

} // namespace cad::rt
