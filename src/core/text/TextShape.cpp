#include "text/TextShape.h"

#include "base/KernelLock.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <Font_FontMgr.hxx>
#include <Font_SystemFont.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <NCollection_String.hxx>
#include <StdPrs_BRepFont.hxx>
#include <TColStd_SequenceOfHAsciiString.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>

namespace cad {

namespace {

constexpr const char *kFallbackFont = "DejaVu Sans";

std::mutex &cacheMutex() {
    static std::mutex m;
    return m;
}

std::vector<std::string> &registeredNames() {
    static std::vector<std::string> names;
    return names;
}

Font_FontAspect aspectOf(const TextStyle &s) {
    if(s.bold && s.italic) return Font_FontAspect_BoldItalic;
    if(s.bold) return Font_FontAspect_Bold;
    if(s.italic) return Font_FontAspect_Italic;
    return Font_FontAspect_Regular;
}

// A loaded font at a size (StdPrs_BRepFont caches its glyphs), per family,
// aspect and size.
Handle(StdPrs_BRepFont) loadFont(const std::string &family, Font_FontAspect aspect, double size) {
    static std::map<std::string, Handle(StdPrs_BRepFont)> fonts;
    std::ostringstream k;
    k << family << '|' << int(aspect) << '|' << size;
    auto it = fonts.find(k.str());
    if(it != fonts.end()) return it->second;
    // Only this family (in the aspect wanted, else any of its aspects).
    Handle(Font_FontMgr) mgr = Font_FontMgr::GetInstance();
    Font_FontAspect found = aspect;
    Handle(Font_SystemFont) sys = mgr->FindFont(TCollection_AsciiString(family.c_str()), Font_StrictLevel_Strict, found,
                                                Standard_False);
    if(sys.IsNull()) return nullptr;
    Handle(StdPrs_BRepFont) font = new StdPrs_BRepFont();
    bool synthItalic = false;
    int faceId = 0;
    const TCollection_AsciiString path = sys->FontPathAny(found, synthItalic, faceId);
    if(path.IsEmpty() || !font->Init(NCollection_String(path.ToCString()), size, faceId)) return nullptr;
    if(fonts.size() > 32) fonts.clear();
    fonts[k.str()] = font;
    return font;
}

// The points of a wire, in order, in the XY plane.
std::vector<Vec2> wirePoints(const TopoDS_Wire &wire, const TopoDS_Face &face, double deflection) {
    std::vector<Vec2> pts;
    for(BRepTools_WireExplorer ex(wire, face); ex.More(); ex.Next()) {
        const TopoDS_Edge &e = ex.Current();
        BRepAdaptor_Curve c(e);
        GCPnts_QuasiUniformDeflection d(c, deflection);
        if(!d.IsDone() || d.NbPoints() < 2) continue;
        std::vector<Vec2> seg;
        for(int i = 1; i <= d.NbPoints(); ++i) {
            const gp_Pnt p = d.Value(i);
            seg.push_back({p.X(), p.Y()});
        }
        if(e.Orientation() == TopAbs_REVERSED) std::reverse(seg.begin(), seg.end());
        for(const Vec2 &p : seg)
            if(pts.empty() || distance(pts.back(), p) > 1e-9) pts.push_back(p);
    }
    if(pts.size() > 1 && distance(pts.front(), pts.back()) < 1e-9) pts.pop_back();
    return pts;
}

double signedArea(const std::vector<Vec2> &pts) {
    double a = 0.0;
    for(size_t i = 0; i < pts.size(); ++i) a += pts[i].cross(pts[(i + 1) % pts.size()]);
    return a * 0.5;
}

std::shared_ptr<TextShape> build(const std::string &utf8, const TextStyle &style) {
    auto out = std::make_shared<TextShape>();
    if(style.size <= 0.0) {
        out->error = "the text size must be more than 0";
        return out;
    }
    KernelLock lock(kernelMutex());
    const Font_FontAspect aspect = aspectOf(style);
    std::string family = style.font.empty() ? std::string(kFallbackFont) : style.font;
    Handle(StdPrs_BRepFont) font = loadFont(family, aspect, style.size);
    if(font.IsNull()) {
        font = loadFont(kFallbackFont, aspect, style.size);
        if(font.IsNull()) {
            out->error = "no font available (not even " + std::string(kFallbackFont) + ")";
            return out;
        }
        out->warning = "font \"" + family + "\" is not installed here: " + kFallbackFont + " is used instead";
    }

    // One line after another; letters at the pen, the pen moved on by the
    // advance (with kerning) plus the letter spacing.
    std::vector<std::vector<Standard_Utf32Char>> lines(1);
    const NCollection_String str(utf8.c_str());
    for(NCollection_Utf8Iter it = str.Iterator(); *it != 0; ++it) {
        const Standard_Utf32Char c = *it;
        if(c == '\n') lines.emplace_back();
        else if(c != '\r') lines.back().push_back(c);
    }
    BRep_Builder bb;
    TopoDS_Compound all;
    bb.MakeCompound(all);
    bool any = false;
    std::string missing;
    for(size_t li = 0; li < lines.size(); ++li) {
        const double y = -double(li) * style.lineSpacing * style.size;
        double x = 0.0;
        const auto &line = lines[li];
        for(size_t i = 0; i < line.size(); ++i) {
            const Standard_Utf32Char c = line[i], next = i + 1 < line.size() ? line[i + 1] : 0;
            const TopoDS_Shape glyph = font->RenderGlyph(c);
            if(!glyph.IsNull()) {
                gp_Trsf t;
                t.SetTranslation(gp_Vec(x, y, 0));
                bb.Add(all, glyph.Moved(TopLoc_Location(t)));
                any = true;
            } else if(c > ' ' && missing.empty()) {
                missing = std::string(1, char(c < 128 ? c : '?'));
            }
            x += font->AdvanceX(c, next) + style.letterSpacing;
        }
    }
    if(!any) {
        out->error = utf8.empty() ? "type some text" : "the font has no letters for this text";
        return out;
    }
    if(!missing.empty() && out->warning.empty()) out->warning = "the font has no outline for '" + missing + "'";

    // Faces into one shape (moved copies), mirrored if asked.
    TopoDS_Shape shape = BRepBuilderAPI_Transform(all, gp_Trsf(), true).Shape();
    const double deflection = std::clamp(style.size * 0.001, 0.002, 0.02);
    std::vector<std::vector<size_t>> regions;
    auto outlines = [&](const TopoDS_Shape &s) {
        std::vector<std::vector<Vec2>> loops;
        regions.clear();
        for(TopExp_Explorer fx(s, TopAbs_FACE); fx.More(); fx.Next()) {
            const TopoDS_Face f = TopoDS::Face(fx.Current());
            const TopoDS_Wire outer = BRepTools::OuterWire(f);
            std::vector<size_t> region;
            for(TopExp_Explorer wx(f, TopAbs_WIRE); wx.More(); wx.Next()) {
                const TopoDS_Wire w = TopoDS::Wire(wx.Current());
                std::vector<Vec2> pts = wirePoints(w, f, deflection);
                if(pts.size() < 3) continue;
                const bool isOuter = w.IsSame(outer);
                const double a = signedArea(pts);
                if((isOuter && a < 0) || (!isOuter && a > 0)) std::reverse(pts.begin(), pts.end());
                // The outer loop first.
                if(isOuter) region.insert(region.begin(), loops.size());
                else region.push_back(loops.size());
                loops.push_back(std::move(pts));
            }
            if(!region.empty() && signedArea(loops[region.front()]) > 0) regions.push_back(std::move(region));
        }
        return loops;
    };
    auto extent = [](const std::vector<std::vector<Vec2>> &loops, Vec2 &lo, Vec2 &hi) {
        lo = {1e300, 1e300};
        hi = {-1e300, -1e300};
        for(const auto &l : loops)
            for(const Vec2 &p : l) {
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
            }
    };
    std::vector<std::vector<Vec2>> loops = outlines(shape);
    extent(loops, out->min, out->max);
    if(style.mirror) {
        // Flipped about the middle of the text, so it stays where it was.
        const double cx = (out->min.x + out->max.x) * 0.5;
        gp_Trsf m;
        m.SetMirror(gp_Ax2(gp_Pnt(cx, 0, 0), gp_Dir(1, 0, 0)));
        TopoDS_Compound flipped;
        bb.MakeCompound(flipped);
        const TopoDS_Shape moved = BRepBuilderAPI_Transform(shape, m, true).Shape();
        // Mirroring turns the faces over: turn them back to face +Z.
        for(TopExp_Explorer fx(moved, TopAbs_FACE); fx.More(); fx.Next()) bb.Add(flipped, fx.Current().Reversed());
        shape = flipped;
        loops = outlines(shape);
        extent(loops, out->min, out->max);
    }
    out->faces = shape;
    out->loops = std::move(loops);
    out->regions = std::move(regions);
    out->ok = true;
    return out;
}

} // namespace

std::string TextStyle::key() const {
    std::ostringstream k;
    k.precision(12);
    k << font << '|' << bold << italic << '|' << size << '|' << letterSpacing << '|' << lineSpacing << '|' << mirror;
    return k.str();
}

std::shared_ptr<const TextShape> buildText(const std::string &utf8, const TextStyle &style) {
    static std::map<std::string, std::shared_ptr<const TextShape>> cache;
    const std::string key = style.key() + '#' + utf8;
    {
        std::lock_guard<std::mutex> lock(cacheMutex());
        auto it = cache.find(key);
        if(it != cache.end()) return it->second;
    }
    std::shared_ptr<const TextShape> made = build(utf8, style);
    std::lock_guard<std::mutex> lock(cacheMutex());
    if(cache.size() > 256) cache.clear();
    cache[key] = made;
    return made;
}

void registerFontDirectory(const std::string &dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if(!fs::is_directory(dir, ec)) return;
    KernelLock lock(kernelMutex());
    Handle(Font_FontMgr) mgr = Font_FontMgr::GetInstance();
    std::vector<fs::path> files;
    for(const auto &e : fs::directory_iterator(dir, ec)) {
        const std::string ext = e.path().extension().string();
        if(ext == ".ttf" || ext == ".otf" || ext == ".TTF" || ext == ".OTF") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for(const auto &f : files) {
        Handle(Font_SystemFont) font = mgr->CheckFont(f.string().c_str());
        if(font.IsNull()) continue;
        mgr->RegisterFont(font, Standard_False); // merges styles into a family (true would replace it)
        const std::string name = font->FontName().ToCString();
        auto &names = registeredNames();
        if(std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
}

std::vector<std::string> availableFonts() {
    KernelLock lock(kernelMutex());
    std::vector<std::string> out = registeredNames();
    TColStd_SequenceOfHAsciiString names;
    Font_FontMgr::GetInstance()->GetAvailableFontsNames(names);
    std::vector<std::string> system;
    for(int i = 1; i <= names.Length(); ++i) {
        const std::string n = names.Value(i)->ToCString();
        if(std::find(out.begin(), out.end(), n) == out.end()) system.push_back(n);
    }
    std::sort(system.begin(), system.end());
    system.erase(std::unique(system.begin(), system.end()), system.end());
    out.insert(out.end(), system.begin(), system.end());
    return out;
}

} // namespace cad
