#include "render/Materials.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace cad {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

constexpr unsigned kAny = (1u << int(Finish::Matte)) | (1u << int(Finish::Silk)) | (1u << int(Finish::Translucent));
constexpr unsigned kMatte = 1u << int(Finish::Matte);
constexpr unsigned kSilk = 1u << int(Finish::Silk);
constexpr unsigned kClear = 1u << int(Finish::Translucent);

std::array<float, 3> linearOf(uint32_t rgb) {
    return {srgbToLinear(float((rgb >> 16) & 0xff) / 255.0f), srgbToLinear(float((rgb >> 8) & 0xff) / 255.0f),
            srgbToLinear(float(rgb & 0xff) / 255.0f)};
}

} // namespace

const char *toString(PrintMaterial m) {
    switch(m) {
    case PrintMaterial::PLA: return "PLA";
    case PrintMaterial::PETG: return "PETG";
    case PrintMaterial::TPU: return "TPU";
    }
    return "PLA";
}

const char *toString(Finish f) {
    switch(f) {
    case Finish::Matte: return "matte";
    case Finish::Silk: return "silk";
    case Finish::Translucent: return "semitransparent";
    }
    return "matte";
}

bool printMaterialFromString(const std::string &s, PrintMaterial &out) {
    const std::string l = lower(s);
    if(l == "pla") out = PrintMaterial::PLA;
    else if(l == "petg") out = PrintMaterial::PETG;
    else if(l == "tpu") out = PrintMaterial::TPU;
    else return false;
    return true;
}

bool finishFromString(const std::string &s, Finish &out) {
    const std::string l = lower(s);
    if(l == "matte") out = Finish::Matte;
    else if(l == "silk") out = Finish::Silk;
    else if(l == "semitransparent" || l == "translucent" || l == "transparent") out = Finish::Translucent;
    else return false;
    return true;
}

json BodyMaterial::toJson() const {
    json j{{"material", toString(material)}, {"finish", toString(finish)}, {"color", hexColor(rgb)}};
    if(!colorName.empty()) j["colorName"] = colorName;
    return j;
}

std::optional<BodyMaterial> BodyMaterial::fromJson(const json &j) {
    if(!j.is_object()) return std::nullopt;
    BodyMaterial m;
    if(!printMaterialFromString(jget<std::string>(j, "material", "PLA"), m.material)) return std::nullopt;
    if(!finishFromString(jget<std::string>(j, "finish", "matte"), m.finish)) return std::nullopt;
    std::string name;
    if(!parseColor(jget<std::string>(j, "color", "#9a9c9f"), m.rgb, name)) return std::nullopt;
    m.colorName = jget<std::string>(j, "colorName", "");
    return m;
}

std::string BodyMaterial::describe() const {
    return std::string(toString(material)) + ", " + toString(finish) + ", " +
           (colorName.empty() ? hexColor(rgb) : colorName);
}

BodyMaterial defaultBodyMaterial() {
    BodyMaterial m;
    m.colorName = "Ash Grey";
    m.rgb = findFilamentColor(m.colorName)->rgb;
    return m;
}

// Typical filament colours (approximate sRGB of the printed plastic).
const std::vector<FilamentColor> &filamentColors() {
    static const std::vector<FilamentColor> colors = {
        {"Ash Grey", 0x9a9c9f, kMatte | kSilk},
        {"Jet Black", 0x1b1b1d, kMatte | kSilk},
        {"Snow White", 0xf1f0ea, kMatte},
        {"Signal Red", 0xc4122d, kMatte},
        {"Orange", 0xf26419, kMatte},
        {"Sunflower Yellow", 0xf3c614, kMatte},
        {"Grass Green", 0x3d9b47, kMatte},
        {"Sky Blue", 0x2f8fd4, kMatte},
        {"Cobalt Blue", 0x1f47a6, kMatte},
        {"Purple", 0x6b3fa0, kMatte},
        {"Beige", 0xd9c6a3, kMatte},
        {"Silk Gold", 0xc9a227, kSilk},
        {"Silk Silver", 0xbfc3c8, kSilk},
        {"Silk Copper", 0xb4683a, kSilk},
        {"Silk Red", 0xa3182b, kSilk},
        {"Silk Blue", 0x2a54b6, kSilk},
        {"Silk Green", 0x2e8d5a, kSilk},
        {"Clear", 0xeef3f4, kClear},
        {"Ice Blue", 0x9fd1f2, kClear},
        {"Ruby", 0xc21a36, kClear},
        {"Emerald", 0x2f9e62, kClear},
        {"Amber", 0xe39a2a, kClear},
        {"Smoke", 0x5c6068, kClear},
    };
    return colors;
}

const FilamentColor *findFilamentColor(const std::string &name) {
    const std::string l = lower(name);
    for(const auto &c : filamentColors())
        if(lower(c.name) == l) return &c;
    return nullptr;
}

std::vector<const FilamentColor *> colorsFor(Finish f) {
    std::vector<const FilamentColor *> out;
    for(const auto &c : filamentColors())
        if(c.finishes & (1u << int(f))) out.push_back(&c);
    return out;
}

bool parseColor(const std::string &text, uint32_t &rgb, std::string &name) {
    if(const FilamentColor *c = findFilamentColor(text)) {
        rgb = c->rgb;
        name = c->name;
        return true;
    }
    std::string t = text;
    if(!t.empty() && t[0] == '#') t = t.substr(1);
    if(t.size() != 6 || !std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isxdigit(c); }))
        return false;
    rgb = uint32_t(std::stoul(t, nullptr, 16));
    name.clear();
    return true;
}

std::string hexColor(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%06x", unsigned(rgb & 0xffffff));
    return buf;
}

float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Optics of printed parts. Sources for the numbers, in brief:
// - Refractive indices: PLA ~1.45-1.47, PETG ~1.57, TPU (ester) ~1.50.
// - Gloss: plain PLA prints have medium gloss; "matte" PLA carries mineral
//   fillers that scatter at the surface (gloss < 10 GU at 60 deg), so a high
//   GGX roughness; PETG is glossier than PLA; TPU is a soft satin.
// - Silk PLA owes its sheen to pearlescent (mica) flakes aligned with the
//   extrusion: tinted, partly metallic highlights stretched along the layers
//   (anisotropic: smooth along a bead, rough across the bead crowns).
// - Semitransparent prints: the plastic itself is clear, but light scatters at
//   the interfaces between beads and layers, so a printed part is hazy: PETG
//   the clearest (mean free path of several mm), PLA milkier, TPU milkier still.
//   The colour is the light a ~3 mm wall lets through (Beer-Lambert).
// - Side walls: layer lines are visible on every finish; roughness of the walls
//   grows with the layer height (Ra ~5-25 um at 0.1-0.3 mm) - see PrintSurface.
BodyMaterial withChanges(BodyMaterial m, std::optional<PrintMaterial> material, std::optional<Finish> finish,
                         std::optional<std::pair<uint32_t, std::string>> color) {
    if(material) m.material = *material;
    if(finish && *finish != m.finish) {
        m.finish = *finish;
        const FilamentColor *named = findFilamentColor(m.colorName);
        if(!color && named && !(named->finishes & (1u << int(*finish)))) {
            const auto options = colorsFor(*finish);
            m.rgb = options.front()->rgb;
            m.colorName = options.front()->name;
        }
    }
    if(color) {
        m.rgb = color->first;
        m.colorName = color->second;
    }
    return m;
}

Optics opticsFor(const BodyMaterial &m) {
    Optics o;
    const auto c = linearOf(m.rgb);
    o.albedo = c;
    o.specTint = {1.0f, 1.0f, 1.0f};
    switch(m.material) {
    case PrintMaterial::PLA: o.ior = 1.46f; break;
    case PrintMaterial::PETG: o.ior = 1.57f; break;
    case PrintMaterial::TPU: o.ior = 1.50f; break;
    }
    switch(m.finish) {
    case Finish::Matte:
        o.roughness = o.roughnessAcross = m.material == PrintMaterial::PETG ? 0.55f : m.material == PrintMaterial::TPU ? 0.62f : 0.68f;
        o.microRoughness = m.material == PrintMaterial::PLA ? 0.55f : 0.35f;
        o.sheen = m.material == PrintMaterial::TPU ? 0.35f : 0.08f;
        o.layerStrength = m.material == PrintMaterial::TPU ? 0.7f : 0.85f;
        break;
    case Finish::Silk:
        // Part of the light comes off the flakes as specular in the filament's
        // colour (specTint, weighted by metalness), the rest is the dyed body.
        o.albedo = {c[0] * 0.8f, c[1] * 0.8f, c[2] * 0.8f};
        o.specTint = c;
        o.metalness = m.material == PrintMaterial::TPU ? 0.25f : 0.4f;
        o.roughness = m.material == PrintMaterial::TPU ? 0.3f : 0.2f;
        o.roughnessAcross = m.material == PrintMaterial::TPU ? 0.55f : 0.5f;
        o.layerStrength = 1.2f;
        o.microRoughness = 0.05f;
        break;
    case Finish::Translucent: {
        float clarity = 1.0f; // mean free path scale
        switch(m.material) {
        case PrintMaterial::PETG:
            o.transmission = 0.94f, o.scatterMm = 14.0f, o.roughness = 0.08f, o.phaseG = 0.65f, clarity = 1.0f;
            break;
        case PrintMaterial::PLA:
            o.transmission = 0.86f, o.scatterMm = 4.0f, o.roughness = 0.16f, o.phaseG = 0.45f, clarity = 0.8f;
            break;
        case PrintMaterial::TPU:
            o.transmission = 0.8f, o.scatterMm = 2.5f, o.roughness = 0.32f, o.phaseG = 0.3f, clarity = 0.7f;
            o.sheen = 0.15f;
            break;
        }
        o.roughnessAcross = o.roughness;
        // Colour = what a 3 mm wall transmits.
        for(int i = 0; i < 3; ++i) o.absorbPerMm[size_t(i)] = -std::log(std::clamp(c[size_t(i)], 0.02f, 1.0f)) / (3.0f * clarity);
        // The diffuse part (light scattered back out) takes the colour, paler.
        o.albedo = {0.5f + 0.5f * c[0], 0.5f + 0.5f * c[1], 0.5f + 0.5f * c[2]};
        o.layerStrength = 1.0f;
        break;
    }
    }
    return o;
}

const char *toString(BuildPlateKind k) {
    switch(k) {
    case BuildPlateKind::TexturedPEI: return "textured_pei";
    case BuildPlateKind::SmoothPEI: return "smooth_pei";
    case BuildPlateKind::None: return "none";
    }
    return "textured_pei";
}
const char *toString(Lighting l) { return l == Lighting::Daylight ? "daylight" : "studio"; }
const char *toString(Placement p) { return p == Placement::AsModelled ? "as_modelled" : "centered"; }
const char *toString(RenderQuality q) { return q == RenderQuality::Final ? "final" : "draft"; }

bool RenderSettings::operator==(const RenderSettings &o) const {
    return plate == o.plate && lighting == o.lighting && placement == o.placement && layerHeight == o.layerHeight &&
           lineWidth == o.lineWidth && layerLines == o.layerLines && rayTraced == o.rayTraced && quality == o.quality;
}

json RenderSettings::toJson() const {
    return json{{"plate", toString(plate)},          {"lighting", toString(lighting)},
                {"placement", toString(placement)},  {"layerHeight", layerHeight},
                {"lineWidth", lineWidth},            {"layerLines", layerLines},
                {"rayTraced", rayTraced},            {"quality", toString(quality)}};
}

RenderSettings RenderSettings::fromJson(const json &j) {
    RenderSettings s;
    if(!j.is_object()) return s;
    const std::string plate = jget<std::string>(j, "plate", toString(s.plate));
    s.plate = plate == "smooth_pei" ? BuildPlateKind::SmoothPEI : plate == "none" ? BuildPlateKind::None : BuildPlateKind::TexturedPEI;
    s.lighting = jget<std::string>(j, "lighting", "studio") == "daylight" ? Lighting::Daylight : Lighting::Studio;
    s.placement = jget<std::string>(j, "placement", "centered") == "as_modelled" ? Placement::AsModelled : Placement::Centered;
    s.layerHeight = std::clamp(jget<double>(j, "layerHeight", s.layerHeight), 0.04, 0.6);
    s.lineWidth = std::clamp(jget<double>(j, "lineWidth", s.lineWidth), 0.1, 2.0);
    s.layerLines = jget<bool>(j, "layerLines", s.layerLines);
    s.rayTraced = jget<bool>(j, "rayTraced", s.rayTraced);
    s.quality = jget<std::string>(j, "quality", "draft") == "final" ? RenderQuality::Final : RenderQuality::Draft;
    return s;
}

} // namespace cad
