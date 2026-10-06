// Print materials per body and render settings: saved, undone, inherited by
// the pieces of a split body; and the optics they render with.
#include <doctest.h>

#include "TestModels.h"
#include "render/Materials.h"

#include <cmath>

using namespace cadtest;

namespace {

BodyMaterial mat(PrintMaterial m, Finish f, const char *color) {
    BodyMaterial b;
    b.material = m;
    b.finish = f;
    std::string name;
    REQUIRE(parseColor(color, b.rgb, name));
    b.colorName = name;
    return b;
}

} // namespace

TEST_CASE("body materials are saved, undone and default to grey matte PLA") {
    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {20, 10}));
    doc.addFeature(extrudeAll(doc, s, "5 mm"));
    CHECK_FALSE(doc.explicitBodyMaterial("b2"));
    CHECK((doc.bodyMaterial("b2") == defaultBodyMaterial()));
    CHECK((defaultBodyMaterial().describe() == "PLA, matte, Ash Grey"));

    const BodyMaterial red = mat(PrintMaterial::PETG, Finish::Translucent, "Ruby");
    doc.setBodyMaterial("b2", red);
    CHECK((doc.bodyMaterial("b2") == red));
    CHECK((doc.undoLabel() == "Body Material"));
    // The same again records nothing.
    const size_t before = doc.toJson().dump().size();
    doc.setBodyMaterial("b2", red);
    CHECK(doc.toJson().dump().size() == before);

    // Pieces of a split body take their parent's material unless given their own.
    CHECK((doc.bodyMaterial("b2.2") == red));
    const BodyMaterial gold = mat(PrintMaterial::PLA, Finish::Silk, "Silk Gold");
    doc.setBodyMaterial("b2.2", gold);
    CHECK((doc.bodyMaterial("b2.2") == gold));
    CHECK((doc.bodyMaterial("b2") == red));

    RenderSettings rs;
    rs.plate = BuildPlateKind::SmoothPEI;
    rs.layerHeight = 0.12;
    rs.lighting = Lighting::Daylight;
    doc.setRenderSettings(rs);

    // Bodies there already that would follow (b2.2 after b2) keep their own look
    // when told which bodies exist.
    Document d0;
    d0.setBodyMaterial("b5", red, {"b5", "b5.2", "b6"});
    CHECK((d0.bodyMaterial("b5.2") == defaultBodyMaterial()));
    CHECK((d0.bodyMaterial("b5.3") == red)); // made later: inherits
    CHECK((d0.bodyMaterial("b6") == defaultBodyMaterial()));

    // Saved and read back.
    const json j = doc.toJson();
    Document d2;
    std::string err;
    REQUIRE(d2.fromJson(j, err));
    CHECK((d2.bodyMaterial("b2") == red));
    CHECK((d2.bodyMaterial("b2.2") == gold));
    CHECK((d2.renderSettings() == rs));
    CHECK((d2.toJson().dump() == j.dump()));

    // Undo puts the previous material back; back to the default.
    REQUIRE(doc.undo());
    CHECK((doc.bodyMaterial("b2.2") == red));
    doc.setBodyMaterials({"b2", "b2.2"}, std::nullopt);
    CHECK((doc.bodyMaterial("b2") == defaultBodyMaterial()));

    // A file from before materials loads with the defaults.
    json old = j;
    old.erase("bodyMaterials");
    old.erase("render");
    Document d3;
    REQUIRE(d3.fromJson(old, err));
    CHECK((d3.bodyMaterial("b2") == defaultBodyMaterial()));
    CHECK((d3.renderSettings() == RenderSettings()));
}

TEST_CASE("material names, colours and parsing") {
    PrintMaterial m;
    Finish f;
    CHECK(printMaterialFromString("petg", m));
    CHECK((m == PrintMaterial::PETG));
    CHECK(finishFromString("Semitransparent", f));
    CHECK((f == Finish::Translucent));
    CHECK_FALSE(printMaterialFromString("ABS", m));
    uint32_t rgb;
    std::string name;
    CHECK(parseColor("#FF8000", rgb, name));
    CHECK(rgb == 0xff8000);
    CHECK(name.empty());
    CHECK(parseColor("silk gold", rgb, name));
    CHECK((name == "Silk Gold"));
    CHECK_FALSE(parseColor("not a colour", rgb, name));
    CHECK((hexColor(0x0a0b0c) == "#0a0b0c"));
    // Every finish has colours to pick from.
    for(Finish fi : {Finish::Matte, Finish::Silk, Finish::Translucent}) CHECK(colorsFor(fi).size() >= 5);
}

TEST_CASE("optics are physical and follow the finish") {
    for(PrintMaterial pm : {PrintMaterial::PLA, PrintMaterial::PETG, PrintMaterial::TPU}) {
        const Optics matte = opticsFor(mat(pm, Finish::Matte, "Signal Red"));
        const Optics silk = opticsFor(mat(pm, Finish::Silk, "Silk Red"));
        const Optics clear = opticsFor(mat(pm, Finish::Translucent, "Ruby"));
        for(const Optics &o : {matte, silk, clear}) {
            CHECK(o.ior > 1.4f);
            CHECK(o.ior < 1.6f);
            CHECK(o.roughness > 0.0f);
            CHECK(o.roughness <= 1.0f);
            for(float a : o.albedo) CHECK((a >= 0.0f && a <= 1.0f));
        }
        // Matte is rougher than silk; silk is anisotropic and metallic-tinted; only
        // the semitransparent finish lets light through.
        CHECK(matte.roughness > silk.roughness);
        CHECK(silk.roughnessAcross > silk.roughness);
        CHECK(silk.metalness > 0.2f);
        CHECK(matte.transmission == 0.0f);
        CHECK(clear.transmission > 0.5f);
        // Ruby lets red through and absorbs green and blue.
        CHECK(clear.absorbPerMm[0] < clear.absorbPerMm[1]);
        CHECK(clear.absorbPerMm[0] < clear.absorbPerMm[2]);
    }
    // PETG is the clearest of the three.
    const float petg = opticsFor(mat(PrintMaterial::PETG, Finish::Translucent, "Clear")).scatterMm;
    const float pla = opticsFor(mat(PrintMaterial::PLA, Finish::Translucent, "Clear")).scatterMm;
    const float tpu = opticsFor(mat(PrintMaterial::TPU, Finish::Translucent, "Clear")).scatterMm;
    CHECK(petg > pla);
    CHECK(pla > tpu);
    // sRGB round trip.
    for(float c : {0.0f, 0.02f, 0.5f, 1.0f}) CHECK(linearToSrgb(srgbToLinear(c)) == doctest::Approx(c).epsilon(1e-4));
}

TEST_CASE("changing a material keeps what is not given; several bodies are one undo step") {
    const BodyMaterial gold = mat(PrintMaterial::PLA, Finish::Silk, "Silk Gold");
    // Only the filament.
    BodyMaterial m = withChanges(gold, PrintMaterial::PETG, std::nullopt, std::nullopt);
    CHECK((m.material == PrintMaterial::PETG));
    CHECK((m.colorName == "Silk Gold"));
    // A finish the colour does not come in: that finish's first colour.
    m = withChanges(gold, std::nullopt, Finish::Translucent, std::nullopt);
    CHECK((m.finish == Finish::Translucent));
    CHECK((m.colorName == colorsFor(Finish::Translucent).front()->name));
    // A colour given with the finish wins.
    m = withChanges(gold, std::nullopt, Finish::Matte, std::make_pair(0x123456u, std::string()));
    CHECK(m.rgb == 0x123456u);
    CHECK(m.colorName.empty());

    Document doc;
    const FeatureId s = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {20, 10}));
    doc.addFeature(extrudeAll(doc, s, "5 mm"));
    const FeatureId s2 = doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {30, 0}, {40, 10}));
    doc.addFeature(extrudeAll(doc, s2, "5 mm"));
    const std::vector<std::pair<BodyId, std::optional<BodyMaterial>>> changes = {
        {"b2", gold}, {"b4", mat(PrintMaterial::TPU, Finish::Matte, "Jet Black")}};
    doc.setBodyMaterials(changes);
    CHECK((doc.bodyMaterial("b2") == gold));
    CHECK((doc.bodyMaterial("b4").material == PrintMaterial::TPU));
    doc.undo();
    CHECK((doc.bodyMaterial("b2") == defaultBodyMaterial()));
    CHECK((doc.bodyMaterial("b4") == defaultBodyMaterial()));
}
