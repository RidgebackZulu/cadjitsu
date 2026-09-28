// Text engraved into and embossed onto faces, flat and curved.
#include <doctest.h>

#include "TestModels.h"
#include "features/TextFeature.h"
#include "io/StlWriter.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <cmath>

using namespace cadtest;

namespace {

// A 60 x 30 x 10 box, or a cylinder of radius 15 and height 40.
struct Setup {
    Document doc;
    explicit Setup(bool cylinder = false) {
        const FeatureId s = cylinder ? doc.addFeature(circleSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, 15))
                                     : doc.addFeature(rectSketch(PlaneRef::origin(PlaneRef::Kind::XY), {0, 0}, {60, 30}));
        doc.addFeature(extrudeAll(doc, s, cylinder ? "40" : "10"));
    }
    const Body &body() { return *onlyBody(doc.displayedState()); }
    TopoRef planar(gp_Dir n) { return makeTopoRef(body(), TopoKind::Face, planarFaceWithNormal(body(), n)); }
    TopoRef side() {
        return makeTopoRef(body(), TopoKind::Face, findFace(body(), [](const TopoDS_Face &f) {
                               return BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder;
                           }));
    }
    std::shared_ptr<TextFeature> text(const TopoRef &face, const std::string &t, const std::string &depth = "1 mm") {
        auto f = std::make_shared<TextFeature>();
        f->face = face;
        f->text = t;
        f->size = doc.makeSlot("8 mm");
        f->x = doc.makeSlot("30 mm");
        f->y = doc.makeSlot("15 mm");
        f->rotation = doc.makeSlot("0 deg");
        f->depth = doc.makeSlot(depth);
        return f;
    }
    double volume() { return totalVolume(doc.displayedState()); }
};

double letterArea(const std::string &t, double size) {
    TextStyle st;
    st.size = size;
    const auto s = buildText(t, st);
    GProp_GProps g;
    BRepGProp::SurfaceProperties(s->faces, g);
    return std::fabs(g.Mass());
}

bool watertight(const StatePtr &st) {
    std::vector<TopoDS_Shape> shapes;
    for(const auto &kv : st->bodies) shapes.push_back(kv.second->shape.shape());
    StlExport ex;
    std::string err;
    return buildStlMesh(shapes, StlOptions(), ex, err) && ex.report.watertight;
}

} // namespace

TEST_CASE("text engraved into a flat face takes out its letters' area times the depth") {
    Setup s;
    const double v0 = s.volume();
    auto f = s.text(s.planar(gp_Dir(0, 0, 1)), "AB");
    s.doc.addFeature(f);
    INFO(s.doc.statusOf(f->id).message);
    REQUIRE(s.doc.statusOf(f->id).isOk());
    CHECK(s.volume() == doctest::Approx(v0 - letterArea("AB", 8) * 1.0).epsilon(1e-4));
    CHECK(watertight(s.doc.displayedState()));
    // The letters sit around (30, 15) on the top face: the cut stays inside it.
    const Bnd_Box box = boundingBox(s.body().shape.shape());
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    CHECK(z1 == doctest::Approx(10).epsilon(1e-6));

    // Embossed instead: the same volume is added, standing 1 mm proud.
    auto g = std::static_pointer_cast<TextFeature>(f->clone());
    g->direction = TextDirection::Emboss;
    s.doc.replaceFeature(g);
    REQUIRE(s.doc.statusOf(g->id).isOk());
    CHECK(s.volume() == doctest::Approx(v0 + letterArea("AB", 8) * 1.0).epsilon(1e-3));
    boundingBox(s.body().shape.shape()).Get(x0, y0, z0, x1, y1, z1);
    CHECK(z1 == doctest::Approx(11).epsilon(1e-6));
    CHECK(watertight(s.doc.displayedState()));
}

TEST_CASE("text is placed by its middle and turned about it") {
    Setup s;
    FaceTextFrame frame;
    std::string err;
    const TopoDS_Face top = s.body().shape.face(planarFaceWithNormal(s.body(), gp_Dir(0, 0, 1)));
    REQUIRE(faceTextFrame(top, frame, err));
    // On a top face the frame is world X and Y.
    CHECK(frame.point(12, 7).Distance(gp_Pnt(12, 7, 10)) < 1e-9);
    TextPlacementInput in;
    in.text = "IIIIII";
    in.style.size = 6;
    in.x = 20;
    in.y = 10;
    auto centre = [&](const TextOnFace &t, double &w, double &h) {
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for(const auto &l : t.outlines)
            for(const gp_Pnt &p : l) {
                x0 = std::min(x0, p.X());
                y0 = std::min(y0, p.Y());
                x1 = std::max(x1, p.X());
                y1 = std::max(y1, p.Y());
            }
        w = x1 - x0;
        h = y1 - y0;
        return gp_Pnt2d(0.5 * (x0 + x1), 0.5 * (y0 + y1));
    };
    double w, h;
    TextOnFace laid = layTextOnFace(top, in);
    REQUIRE(laid.ok);
    CHECK(centre(laid, w, h).Distance(gp_Pnt2d(20, 10)) < 1e-6);
    CHECK(w > h); // a row of letters
    in.rotation = M_PI / 2;
    laid = layTextOnFace(top, in);
    double w2, h2;
    CHECK(centre(laid, w2, h2).Distance(gp_Pnt2d(20, 10)) < 1e-6);
    CHECK(w2 == doctest::Approx(h).epsilon(1e-6));
    CHECK(h2 == doctest::Approx(w).epsilon(1e-6));
    // Off the face: a warning.
    in.x = 58;
    CHECK(layTextOnFace(top, in).warning.find("edge of the face") != std::string::npos);
}

TEST_CASE("text on an upright face reads from outside, y up") {
    Setup s;
    const TopoDS_Face front = s.body().shape.face(planarFaceWithNormal(s.body(), gp_Dir(0, -1, 0)));
    FaceTextFrame frame;
    std::string err;
    REQUIRE(faceTextFrame(front, frame, err));
    CHECK(frame.plane.XDirection().IsEqual(gp_Dir(1, 0, 0), 1e-9));
    CHECK(frame.plane.YDirection().IsEqual(gp_Dir(0, 0, 1), 1e-9));
    const TopoDS_Face back = s.body().shape.face(planarFaceWithNormal(s.body(), gp_Dir(0, 1, 0)));
    REQUIRE(faceTextFrame(back, frame, err));
    CHECK(frame.plane.XDirection().IsEqual(gp_Dir(-1, 0, 0), 1e-9));
    CHECK(frame.plane.YDirection().IsEqual(gp_Dir(0, 0, 1), 1e-9));
}

TEST_CASE("text wraps around a cylinder, following it") {
    for(TextDirection dir : {TextDirection::Engrave, TextDirection::Emboss}) {
        Setup s(true);
        const double v0 = s.volume();
        auto f = s.text(s.side(), "CAD", "0.8 mm");
        f->x = s.doc.makeSlot("0 mm");
        f->y = s.doc.makeSlot("0 mm");
        f->direction = dir;
        s.doc.addFeature(f);
        INFO(s.doc.statusOf(f->id).message);
        REQUIRE(s.doc.statusOf(f->id).isOk());
        // On a cylinder the letters' area at radius R is exact; the cut or
        // raised material lies between R and R -/+ depth.
        const double a = letterArea("CAD", 8), R = 15, d = 0.8;
        const double expect = dir == TextDirection::Engrave ? a * (R - d / 2) / R * d : a * (R + d / 2) / R * d;
        const double change = std::fabs(s.volume() - v0);
        CHECK(change == doctest::Approx(expect).epsilon(0.01));
        CHECK(watertight(s.doc.displayedState()));
        // Every vertex of the result is on the cylinder, inside it or just out by the depth.
        for(TopExp_Explorer vx(s.body().shape.shape(), TopAbs_VERTEX); vx.More(); vx.Next()) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vx.Current()));
            const double r = std::hypot(p.X(), p.Y());
            CHECK(r <= R + (dir == TextDirection::Emboss ? d : 0.0) + 1e-4);
        }
    }
}

TEST_CASE("text feature: saved and read back, and its errors") {
    Setup s;
    auto f = s.text(s.planar(gp_Dir(0, 0, 1)), "Hi\nthere");
    f->font = "DejaVu Serif";
    f->bold = true;
    f->mirror = true;
    f->direction = TextDirection::Emboss;
    f->lineSpacing = s.doc.makeSlot("1.5");
    const FeatureId id = s.doc.addFeature(f);
    INFO(s.doc.statusOf(id).message);
    REQUIRE(s.doc.statusOf(id).isOk());
    const json j = f->toJson();
    std::string err;
    auto back = std::dynamic_pointer_cast<TextFeature>(Feature::fromJson(j, &err));
    REQUIRE(back);
    CHECK(back->text == "Hi\nthere");
    CHECK(back->font == std::string("DejaVu Serif"));
    CHECK(back->bold);
    CHECK(back->mirror);
    CHECK(back->text == std::string("Hi\nthere"));
    CHECK(back->lineSpacing.expr == f->lineSpacing.expr);
    CHECK((back->face.toJson() == f->face.toJson()));

    auto empty = std::static_pointer_cast<TextFeature>(f->clone());
    empty->text = "  ";
    s.doc.replaceFeature(empty);
    CHECK(s.doc.statusOf(id).message.find("type some text") != std::string::npos);
}

// Kerned letters overlap (R's leg runs under A); on this cylinder the
// surface's axis points down, and the text still reads upright.
TEST_CASE("kerned text on a cylinder cuts only its letters, upright") {
    Document doc;
    auto sk = std::make_shared<SketchFeature>();
    sk->plane = PlaneRef::origin(PlaneRef::Kind::XY);
    sk->sketch.addRectangle({100, 0}, {170, 30});
    sk->sketch.addCircle(Vec2{135, 70}, 15);
    const FeatureId sid = doc.addFeature(sk);
    for(const auto &p : doc.stateAt(doc.marker())->sketches.at(sid)->profiles) {
        if(std::fabs(p.area) > 1000) continue;
        auto ex = std::make_shared<ExtrudeFeature>();
        ex->profiles.push_back({sid, p.key, p.sample});
        ex->distance = doc.makeSlot("40 mm");
        doc.addFeature(ex);
    }
    const Body &b = *onlyBody(doc.displayedState());
    const int fi = findFace(b, [](const TopoDS_Face &f) { return BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder; });
    FaceTextFrame fr;
    std::string err;
    REQUIRE(faceTextFrame(b.shape.face(fi), fr, err));
    gp_Dir xd, yd;
    fr.axes(0, 0, xd, yd);
    CHECK(yd.Z() > 0.99);
    CHECK(xd.Crossed(yd).IsEqual(fr.normal(0, 0), 1e-9));
    CHECK(fr.point(0, 5).Z() > fr.point(0, 0).Z());
    auto f = std::make_shared<TextFeature>();
    f->face = makeTopoRef(b, TopoKind::Face, fi);
    f->text = "WRAP";
    f->size = doc.makeSlot("9 mm");
    f->depth = doc.makeSlot("1 mm");
    const double v0 = totalVolume(doc.displayedState());
    doc.addFeature(f);
    INFO(doc.statusOf(f->id).message);
    REQUIRE(doc.statusOf(f->id).isOk());
    const double cut = v0 - totalVolume(doc.displayedState());
    CHECK(cut > 0.9 * letterArea("WRAP", 9) * (15 - 0.5) / 15 * 1.0 - 2.0);
    CHECK(cut < 1.02 * letterArea("WRAP", 9));
    CHECK(watertight(doc.displayedState()));
}
