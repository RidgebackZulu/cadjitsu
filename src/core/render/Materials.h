#pragma once

#include "base/Json.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cad {

// What a body is printed in: the filament, its finish and colour. Used by the
// renderers (the live preview and the path tracer) to make the body look like
// the printed part.
enum class PrintMaterial { PLA, PETG, TPU };
enum class Finish { Matte, Silk, Translucent };

const char *toString(PrintMaterial m);
const char *toString(Finish f);
bool printMaterialFromString(const std::string &s, PrintMaterial &out);
bool finishFromString(const std::string &s, Finish &out); // also "semitransparent"

struct BodyMaterial {
    PrintMaterial material = PrintMaterial::PLA;
    Finish finish = Finish::Matte;
    uint32_t rgb = 0x9a9c9f;   // sRGB 0xRRGGBB
    std::string colorName;     // a named filament colour, or empty (custom)

    bool operator==(const BodyMaterial &o) const {
        return material == o.material && finish == o.finish && rgb == o.rgb && colorName == o.colorName;
    }
    bool operator!=(const BodyMaterial &o) const { return !(*this == o); }
    json toJson() const;
    static std::optional<BodyMaterial> fromJson(const json &j);
    // "PLA, matte, Ash Grey"
    std::string describe() const;
};

// The look of a body with nothing chosen: grey matte PLA.
BodyMaterial defaultBodyMaterial();
// `m` with what is given changed. A finish change keeps a named colour only if
// that colour comes in the new finish (else it takes the finish's first one).
BodyMaterial withChanges(BodyMaterial m, std::optional<PrintMaterial> material, std::optional<Finish> finish,
                         std::optional<std::pair<uint32_t, std::string>> color);

// Named filament colours. Each suits one or more finishes (`finishes` is a
// bit set of 1 << int(Finish)).
struct FilamentColor {
    const char *name;
    uint32_t rgb;
    unsigned finishes;
};
const std::vector<FilamentColor> &filamentColors();
const FilamentColor *findFilamentColor(const std::string &name); // case-insensitive
std::vector<const FilamentColor *> colorsFor(Finish f);
// "#rrggbb" or a named colour; false if neither.
bool parseColor(const std::string &text, uint32_t &rgb, std::string &name);
std::string hexColor(uint32_t rgb); // "#rrggbb"

// The optical parameters a body renders with, from measured properties of
// printed parts (see Materials.cpp). Colours are linear RGB.
struct Optics {
    std::array<float, 3> albedo{};     // diffuse (body) colour
    std::array<float, 3> specTint{};   // colour of the metallic share of the specular (silk flakes)
    float ior = 1.46f;                 // refractive index (Fresnel)
    float roughness = 0.5f;            // GGX alpha along the layers (perceptual roughness)
    float roughnessAcross = 0.5f;      // across the layers (silk is anisotropic)
    float metalness = 0.0f;            // silk: pearlescent flakes behave partly metallic
    float sheen = 0.0f;                // soft grazing sheen (TPU, matte)
    // Semitransparent finishes.
    float transmission = 0.0f;         // 0 opaque .. 1 clear
    float scatterMm = 1.0f;            // mean free path for scattering, mm
    std::array<float, 3> absorbPerMm{};// absorption coefficient per mm (linear RGB)
    float phaseG = 0.0f;               // Henyey-Greenstein anisotropy of the scattering
    // Surface texture of the print.
    float layerStrength = 1.0f;        // how pronounced the layer lines are (0..1.5)
    float microRoughness = 0.0f;       // extra fine-scale grain (matte fillers)
};

Optics opticsFor(const BodyMaterial &m);

// Linear <-> sRGB (one channel, 0..1).
float srgbToLinear(float c);
float linearToSrgb(float c);

// How the scene is rendered: the build plate, lighting and the print's surface.
// How much plastic a ray crosses on a straight path of `length` mm through a
// printed part (two walls of 3 perimeters, then infill).
float plasticAlong(float length, float lineWidth, float infill);

enum class BuildPlateKind { TexturedPEI, SmoothPEI, None };
enum class Lighting { Studio, Daylight };
enum class Placement { Centered, AsModelled };
enum class RenderQuality { Draft, Final };

const char *toString(BuildPlateKind k);
const char *toString(Lighting l);
const char *toString(Placement p);
const char *toString(RenderQuality q);

struct RenderSettings {
    BuildPlateKind plate = BuildPlateKind::TexturedPEI;
    Lighting lighting = Lighting::Studio;
    Placement placement = Placement::Centered;
    double layerHeight = 0.2;  // mm
    double lineWidth = 0.42;   // mm (nozzle 0.4)
    bool layerLines = true;
    // Semitransparent prints are walls and sparse infill, not solid plastic:
    // light crosses two walls of 3 perimeters, then infill-density plastic.
    double infill = 0.15;      // 0..1
    bool rayTraced = true;     // the path tracer refines the Rendered view when the camera rests
    RenderQuality quality = RenderQuality::Draft;

    bool operator==(const RenderSettings &o) const;
    bool operator!=(const RenderSettings &o) const { return !(*this == o); }
    json toJson() const;
    static RenderSettings fromJson(const json &j); // missing keys keep their defaults
};

} // namespace cad
