#include "mcp/McpTools.h"
#include "mcp/McpLog.h"

#include "MainWindow.h"
#include "io/Exporter.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "viewport/TraceScene.h"
#include "viewport/Viewport.h"

#include "base/KernelLock.h"
#include "doc/Document.h"
#include "doc/Section.h"
#include "features/ChamferFeature.h"
#include "features/CombineFeature.h"
#include "features/DraftFeature.h"
#include "features/ConstructionPlaneFeature.h"
#include "features/ExtrudeFeature.h"
#include "features/FilletFeature.h"
#include "features/HoleFeature.h"
#include "features/PatternFeature.h"
#include "features/RevolveFeature.h"
#include "features/ShellFeature.h"
#include "features/SketchFeature.h"
#include "features/SplitFeature.h"
#include "features/TextFeature.h"
#include "features/ThreadFeature.h"
#include "geom/OcctUtil.h"
#include "measure/Measure.h"
#include "measure/MeasureBetween.h"
#include "measure/Overhang.h"
#include "sketch/SketchOffset.h"
#include "sketch/SketchEdit.h"
#include "sketch/SketchOps.h"
#include "sketch/SketchProject.h"
#include "sketch/SketchSolver.h"
#include "sketch/SketchText.h"
#include "topo/Resolver.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace cadjitsu {

using json = nlohmann::json;

namespace {

// A tool refused (bad arguments, or the model could not be built): its text
// goes back to the agent with isError set, so it can correct itself.
struct ToolError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
[[noreturn]] void fail(const std::string &why) { throw ToolError(why); }

json textResult(const json &payload, bool isError = false) {
    return {{"content", json::array({{{"type", "text"}, {"text", payload.is_string() ? payload.get<std::string>() : payload.dump(2)}}})},
            {"isError", isError}};
}

std::string num(double v) {
    std::ostringstream s;
    s.precision(10);
    s << (std::fabs(v) < 1e-12 ? 0.0 : v);
    return s.str();
}
double r3(double v) { return std::round(v * 1000.0) / 1000.0; }
json pt(const gp_Pnt &p) { return json::array({r3(p.X()), r3(p.Y()), r3(p.Z())}); }
json dir(const gp_Dir &d) { return json::array({r3(d.X()), r3(d.Y()), r3(d.Z())}); }
json v2(cad::Vec2 p) { return json::array({r3(p.x), r3(p.y)}); }

cad::Vec2 vec2Arg(const json &a, const char *what) {
    if(!a.is_array() || a.size() != 2 || !a[0].is_number() || !a[1].is_number())
        fail(std::string(what) + " must be [x, y] (numbers, mm)");
    return {a[0].get<double>(), a[1].get<double>()};
}
gp_Pnt pntArg(const json &a, const char *what) {
    if(!a.is_array() || a.size() != 3 || !a[0].is_number() || !a[1].is_number() || !a[2].is_number())
        fail(std::string(what) + " must be [x, y, z] (numbers, mm)");
    return gp_Pnt(a[0].get<double>(), a[1].get<double>(), a[2].get<double>());
}

// A length / angle argument: a number (mm / degrees) or an expression ("20 mm", "d3 * 2").
std::string lengthExpr(const json &v, const char *what) {
    if(v.is_number()) return num(v.get<double>()) + " mm";
    if(v.is_string() && !v.get<std::string>().empty()) return v.get<std::string>();
    fail(std::string(what) + " must be a number (mm) or an expression");
}
std::string angleExpr(const json &v, const char *what) {
    if(v.is_number()) return num(v.get<double>()) + " deg";
    if(v.is_string() && !v.get<std::string>().empty()) return v.get<std::string>();
    fail(std::string(what) + " must be a number (degrees) or an expression");
}

const char *severity(cad::Severity s) {
    switch(s) {
    case cad::Severity::Ok: return "ok";
    case cad::Severity::Warning: return "warning";
    case cad::Severity::Error: return "error";
    }
    return "ok";
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// --- JSON schema helpers -----------------------------------------------------------------
json numberOrExpr(const std::string &d) {
    return {{"description", d}, {"anyOf", json::array({{{"type", "number"}}, {{"type", "string"}}})}};
}
json str(const std::string &d) { return {{"type", "string"}, {"description", d}}; }
json enumOf(std::vector<std::string> values, const std::string &d) { return {{"type", "string"}, {"enum", values}, {"description", d}}; }
json integer(const std::string &d) { return {{"type", "integer"}, {"description", d}}; }
json boolean(const std::string &d) { return {{"type", "boolean"}, {"description", d}}; }
json xy(const std::string &d) {
    return {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}, {"description", d}};
}
json xyz(const std::string &d) {
    return {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", d}};
}
json arrayOf(json item, const std::string &d) { return {{"type", "array"}, {"items", item}, {"description", d}}; }
json number(const std::string &d) { return {{"type", "number"}, {"description", d}}; }

// A sketch's curves (and lone points), with their ids for the sketch tools.
json curvesJson(const cad::Sketch &s) {
    std::set<int> used;
    for(const auto &e : s.entities)
        if(e.isCurve() || e.isText())
            for(int p : {e.a, e.b, e.c}) used.insert(p);
    json out = json::array();
    auto xy = [&](int p) {
        const cad::Vec2 v = s.pointPos(p);
        return json::array({std::round(v.x * 1000) / 1000, std::round(v.y * 1000) / 1000});
    };
    for(const auto &e : s.entities) {
        json j{{"id", e.id}};
        switch(e.type) {
        case cad::SkType::Line: j["type"] = "line", j["from"] = xy(e.a), j["to"] = xy(e.b); break;
        case cad::SkType::Circle: j["type"] = "circle", j["center"] = xy(e.a), j["radius"] = std::round(e.r * 1000) / 1000; break;
        case cad::SkType::Arc: j["type"] = "arc", j["center"] = xy(e.a), j["start"] = xy(e.b), j["end"] = xy(e.c); break;
        case cad::SkType::Text: j["type"] = "text", j["text"] = e.text, j["origin"] = xy(e.a); break;
        case cad::SkType::Point:
            if(used.count(e.id)) continue;
            j["type"] = "point", j["at"] = xy(e.id);
            break;
        }
        if(e.construction) j["construction"] = true;
        if(e.isProjected()) j["projected_from"] = {{"sketch", e.projSketch}, {"entity", e.projEntity}};
        out.push_back(j);
    }
    return out;
}

// The render settings as the tools name them.
json renderJson(const cad::RenderSettings &s) {
    return {{"plate", cad::toString(s.plate)},         {"placement", cad::toString(s.placement)},
            {"lighting", cad::toString(s.lighting)},   {"layer_height", s.layerHeight},
            {"line_width", s.lineWidth},               {"layer_lines", s.layerLines},
            {"infill_percent", std::lround(s.infill * 100.0)},
            {"ray_traced", s.rayTraced},               {"quality", cad::toString(s.quality)}};
}
json topoItem(const std::string &what) {
    return {{"type", "object"},
            {"properties", {{"body", str("body id (\"b2\") or name (\"Body1\")")}, {"index", integer(what + " index from list_" + what + "s")}}},
            {"required", {"body", "index"}}};
}
json measureItem(const std::string &d) {
    return {{"type", "object"},
            {"description", d + ": {\"body\": \"Body1\"} (the whole body), {\"body\": ..., \"face\": i} / \"edge\" / "
                                "\"vertex\" (indexes from list_faces / list_edges), or {\"point\": [x, y, z]}"},
            {"properties",
             {{"body", str("body id or name")},
              {"face", integer("face index")},
              {"edge", integer("edge index")},
              {"vertex", integer("vertex index")},
              {"point", xyz("a point in mm")}}}};
}
json planeSpec() {
    return {{"description",
             "Where: \"XY\", \"XZ\" or \"YZ\" (origin planes), {\"plane\": <construction plane feature id>}, or "
             "{\"face\": {\"body\": \"b2\", \"index\": 5}} for a planar face of a body."},
            {"anyOf", json::array({{{"type", "string"}}, {{"type", "object"}}})}};
}

} // namespace

McpTools::McpTools(MainWindow &window) : m_w(window) { define(); }

void McpTools::add(const std::string &name, const std::string &description, json properties, std::vector<std::string> required,
                   std::function<json(const json &)> run) {
    json schema = {{"type", "object"}, {"properties", properties.is_null() ? json::object() : properties}};
    if(!required.empty()) schema["required"] = required;
    m_order.push_back(name);
    m_tools[name] = {description, schema, std::move(run)};
}

json McpTools::toolList() const {
    json list = json::array();
    for(const std::string &n : m_order) {
        const Tool &t = m_tools.at(n);
        list.push_back({{"name", n}, {"description", t.description}, {"inputSchema", t.schema}});
    }
    return list;
}

json McpTools::callTool(const std::string &name, const json &args) {
    auto it = m_tools.find(name);
    if(it == m_tools.end()) return textResult("unknown tool: " + name, true);
    try {
        json r = it->second.run(args.is_object() ? args : json::object());
        if(r.contains("content")) return r;
        return textResult(r);
    } catch(const ToolError &e) {
        return textResult(std::string(e.what()), true);
    } catch(const json::exception &e) {
        return textResult(std::string("bad arguments: ") + e.what(), true);
    }
}

void McpTools::define() {
    cad::Document &doc = m_w.document();

    // --- shared helpers ---------------------------------------------------------------
    auto settle = [this, &doc]() -> cad::StatePtr {
        m_w.waitForModel(120000);
        return doc.displayedState();
    };
    auto begin = [this] { m_w.finishInteractions(); };
    auto bodyOf = [&doc](const cad::StatePtr &st, const json &v) -> const cad::Body * {
        if(!v.is_string()) fail("body must be a body id (\"b2\") or name (\"Body1\")");
        const std::string key = v.get<std::string>();
        for(const cad::Body *b : st->orderedBodies())
            if(b->id == key || doc.bodyName(*b) == key) return b;
        fail("no body \"" + key + "\" (see get_design)");
    };
    auto topo = [bodyOf](const cad::StatePtr &st, const json &item, cad::TopoKind kind) {
        const cad::Body *b = bodyOf(st, item.at("body"));
        const int i = item.at("index").get<int>();
        const int n = kind == cad::TopoKind::Face ? b->shape.faceCount() : b->shape.edgeCount();
        if(i < 1 || i > n)
            fail((kind == cad::TopoKind::Face ? "face" : "edge") + std::string(" index ") + std::to_string(i) + " is not in 1.." +
                 std::to_string(n) + " for " + b->id);
        return cad::makeTopoRef(*b, kind, i);
    };
    auto featureOf = [&doc](const json &v) -> cad::FeaturePtr {
        if(v.is_number_integer())
            if(auto f = doc.feature(v.get<int>())) return f;
        if(v.is_string())
            for(const auto &f : doc.features())
                if(f->name == v.get<std::string>()) return f;
        fail("no feature " + v.dump() + " (see get_design)");
    };
    auto checkExpr = [&doc](const std::string &expr, cad::ValueKind kind, const char *what) {
        const cad::EvalResult r = doc.params().evaluateExpression(expr, kind);
        if(!r.ok) fail(std::string(what) + " \"" + expr + "\": " + r.error);
    };
    auto slot = [&doc, checkExpr](const std::string &expr, cad::ValueKind kind, const char *what) {
        checkExpr(expr, kind, what);
        return doc.makeSlot(expr);
    };
    auto planeOf = [topo](const cad::StatePtr &st, const json &v) {
        if(v.is_string()) {
            const std::string p = lower(v.get<std::string>());
            if(p == "xy") return cad::PlaneRef::origin(cad::PlaneRef::Kind::XY);
            if(p == "xz") return cad::PlaneRef::origin(cad::PlaneRef::Kind::XZ);
            if(p == "yz") return cad::PlaneRef::origin(cad::PlaneRef::Kind::YZ);
            fail("plane must be XY, XZ, YZ, {\"plane\": id} or {\"face\": {body, index}}");
        }
        if(v.is_object() && v.contains("plane")) {
            const int id = v["plane"].get<int>();
            if(!st->planes.count(id)) fail("no construction plane " + std::to_string(id));
            return cad::PlaneRef::construction(id);
        }
        if(v.is_object() && v.contains("face")) {
            const cad::TopoRef f = topo(st, v["face"], cad::TopoKind::Face);
            const cad::ResolvedRef r = cad::resolveRef(*st, f);
            gp_Pln pln;
            if(!r.ok || !cad::planeOfFace(TopoDS::Face(r.shape), pln)) fail("that face is not planar");
            return cad::PlaneRef::onFace(f);
        }
        fail("plane must be XY, XZ, YZ, {\"plane\": id} or {\"face\": {body, index}}");
    };
    auto bodiesJson = [&doc](const cad::StatePtr &st) {
        json out = json::array();
        const cad::KernelLock lock(cad::kernelMutex());
        for(const cad::Body *b : st->orderedBodies()) {
            const Bnd_Box box = cad::boundingBox(b->shape.shape());
            json bb;
            if(!box.IsVoid()) {
                double x0, y0, z0, x1, y1, z1;
                box.Get(x0, y0, z0, x1, y1, z1);
                bb = {{"min", {r3(x0), r3(y0), r3(z0)}}, {"max", {r3(x1), r3(y1), r3(z1)}}};
            }
            out.push_back({{"id", b->id},
                           {"name", doc.bodyName(*b)},
                           {"volume_mm3", r3(cad::volumeOf(b->shape.shape()))},
                           {"area_mm2", r3(cad::areaOf(b->shape.shape()))},
                           {"bounding_box", bb},
                           {"faces", b->shape.faceCount()},
                           {"edges", b->shape.edgeCount()},
                           {"visible", doc.bodyVisible(b->id)},
                           {"material", doc.bodyMaterial(b->id).toJson()}});
        }
        return out;
    };
    // What is shown: every body, sketch and construction plane with its own
    // setting and whether it is on screen, and the browser folders.
    auto visibilityJson = [this, &doc](const cad::StatePtr &st) {
        ModelView *view = m_w.modelView();
        json folders = json::object();
        for(const std::string &f : cad::Document::folderNames()) folders[f] = doc.folderVisible(f);
        json bodies = json::array(), sketches = json::array(), planes = json::array();
        for(const cad::Body *b : st->orderedBodies())
            bodies.push_back({{"id", b->id}, {"name", doc.bodyName(*b)}, {"visible", doc.bodyVisible(b->id)},
                              {"shown", doc.bodyVisible(b->id) && doc.folderVisible("bodies")}});
        for(const auto &[id, sk] : st->sketches)
            sketches.push_back({{"id", id}, {"name", sk->name}, {"visible", view->sketchShown(id, true)},
                                {"shown", view->sketchShown(id)}});
        for(const auto &[id, p] : st->planes)
            planes.push_back({{"id", id}, {"name", p->name}, {"visible", doc.planeVisible(id)},
                              {"shown", doc.planeVisible(id) && doc.folderVisible("construction")}});
        return json{{"folders", folders}, {"bodies", bodies}, {"sketches", sketches}, {"construction_planes", planes}};
    };
    // Adds a feature as one undo step; if it cannot be built it is taken out
    // again and the reason returned as an error.
    auto commit = [this, &doc, settle, bodiesJson](std::shared_ptr<cad::Feature> f, const std::string &label) {
        const cad::FeatureId id = doc.addFeature(f, label);
        const cad::StatePtr st = settle();
        const cad::Status s = doc.statusOf(id);
        if(s.isError()) {
            doc.undo();
            settle();
            fail(s.message + " (nothing was added)");
        }
        const cad::FeaturePtr added = doc.feature(id);
        json r = {{"feature", {{"id", id}, {"name", added->name}, {"type", cad::toString(added->type())}}},
                  {"status", severity(s.severity)},
                  {"bodies", bodiesJson(st)}};
        if(!s.message.empty()) r["message"] = s.message;
        return r;
    };
    auto sketchResultOf = [](const cad::StatePtr &st, cad::FeatureId id) -> const cad::SketchResult * {
        auto it = st->sketches.find(id);
        if(it == st->sketches.end()) fail("no sketch " + std::to_string(id) + " (see get_design)");
        return it->second.get();
    };
    auto sketchJson = [](const cad::SketchResult &sk) {
        json profiles = json::array();
        int i = 0;
        for(const auto &p : sk.profiles)
            profiles.push_back({{"index", i++},
                                {"area_mm2", r3(std::fabs(p.area))},
                                {"inside_point", v2(p.sample)},
                                {"inside_point_world", pt(sk.toWorld(p.sample))},
                                {"holes", int(p.holes.size())}});
        return json{{"frame",
                     {{"origin", pt(sk.frame.Location())},
                      {"x_axis", dir(sk.frame.XDirection())},
                      {"y_axis", dir(sk.frame.YDirection())},
                      {"normal", dir(sk.frame.Direction())}}},
                    {"profiles", profiles},
                    {"curves", curvesJson(sk.sketch)},
                    {"degrees_of_freedom", sk.dof},
                    {"status", severity(sk.status.severity)},
                    {"message", sk.status.message}};
    };
    // Sketch entities from their JSON description, dimensioned if asked
    // (the dimensions become parameters an agent can change later).
    auto addEntities = [&doc](cad::Sketch &sk, const json &entities, json &created, json &params) {
        if(!entities.is_array()) fail("entities must be an array");
        auto dimension = [&](cad::SkCon type, int ent, double value, cad::Vec2 label, const std::string &what) {
            const int cid = sk.addConstraint(type, ent);
            cad::SkConstraint *c = sk.findConstraint(cid);
            c->param = doc.allocateParamName();
            c->expr = num(value) + " mm";
            c->label = label;
            params.push_back({{"name", c->param}, {"expr", c->expr}, {"measures", what}});
        };
        for(const json &e : entities) {
            const std::string type = lower(e.value("type", ""));
            const bool construction = e.value("construction", false);
            if(type == "rectangle" || type == "center_rectangle") {
                cad::Vec2 a, b;
                if(type == "rectangle") {
                    a = vec2Arg(e.at("corner1"), "corner1");
                    b = vec2Arg(e.at("corner2"), "corner2");
                } else {
                    const cad::Vec2 c = vec2Arg(e.at("center"), "center");
                    const double w = e.at("width").get<double>(), h = e.at("height").get<double>();
                    a = {c.x - w / 2, c.y - h / 2};
                    b = {c.x + w / 2, c.y + h / 2};
                }
                const double w = std::fabs(b.x - a.x), h = std::fabs(b.y - a.y);
                if(w < 1e-6 || h < 1e-6) fail("a rectangle needs a width and a height");
                const std::vector<int> lines = sk.addRectangle(a, b, construction);
                created.push_back({{"type", "rectangle"}, {"lines", lines}});
                if(e.value("dimension", true) && !construction) {
                    dimension(cad::SkCon::Distance, lines[0], w, {0, -std::max(3.0, h * 0.15)}, "rectangle width");
                    dimension(cad::SkCon::Distance, lines[3], h, {-std::max(3.0, w * 0.15), 0}, "rectangle height");
                    // Pinned at corner1, so changing the width / height moves the far sides.
                    const cad::Vec2 anchor = type == "rectangle" ? vec2Arg(e.at("corner1"), "corner1") : a;
                    int corner = 0;
                    double best = 1e300;
                    for(int l : lines) {
                        const int pid = sk.find(l)->a;
                        const double d = (sk.pointPos(pid) - anchor).length();
                        if(d < best) best = d, corner = pid;
                    }
                    sk.addConstraint(cad::SkCon::Fix, corner);
                }
            } else if(type == "circle") {
                const cad::Vec2 c = vec2Arg(e.at("center"), "center");
                const double r = e.contains("diameter") ? e["diameter"].get<double>() / 2 : e.at("radius").get<double>();
                if(r <= 0) fail("a circle needs a positive radius");
                const int id = sk.addCircle(c, r, construction);
                created.push_back({{"type", "circle"}, {"id", id}});
                if(e.value("dimension", true) && !construction) {
                    dimension(cad::SkCon::Diameter, id, 2 * r, cad::Vec2(0.7071, 0.7071) * (r + 3), "circle diameter");
                    sk.addConstraint(cad::SkCon::Fix, sk.find(id)->a); // the centre stays put
                }
            } else if(type == "line") {
                const cad::Vec2 a = vec2Arg(e.at("from"), "from"), b = vec2Arg(e.at("to"), "to");
                if((b - a).length() < 1e-6) fail("a line needs two different points");
                const int id = sk.addLine(a, b, construction);
                created.push_back({{"type", "line"}, {"id", id}});
                if(e.value("dimension", false))
                    dimension(cad::SkCon::Distance, id, (b - a).length(), (b - a).normalized().perp() * 3.0, "line length");
            } else if(type == "polygon" || type == "polyline") {
                const json &pts = e.at("points");
                if(!pts.is_array() || pts.size() < 2) fail("a polygon needs at least 2 points");
                std::vector<int> ids;
                for(const json &p : pts) {
                    const cad::Vec2 q = vec2Arg(p, "point");
                    ids.push_back(sk.addPoint(q.x, q.y, construction));
                }
                const bool closed = e.value("closed", type == "polygon");
                json lines = json::array();
                for(size_t i = 0; i + 1 < ids.size(); ++i) lines.push_back(sk.addLine(ids[i], ids[i + 1], construction));
                if(closed && ids.size() > 2) lines.push_back(sk.addLine(ids.back(), ids.front(), construction));
                created.push_back({{"type", type}, {"lines", lines}});
            } else if(type == "arc") {
                const cad::Vec2 c = vec2Arg(e.at("center"), "center"), s = vec2Arg(e.at("start"), "start");
                cad::Vec2 t = vec2Arg(e.at("end"), "end");
                const double r = (s - c).length();
                if(r < 1e-6 || (t - c).length() < 1e-6) fail("an arc needs start and end away from its centre");
                t = c + (t - c).normalized() * r; // on the circle
                const int ci = sk.addPoint(c.x, c.y, construction), si = sk.addPoint(s.x, s.y, construction),
                          ti = sk.addPoint(t.x, t.y, construction);
                const int id = sk.addArc(ci, si, ti, construction);
                created.push_back({{"type", "arc"}, {"id", id}});
                if(e.value("dimension", false))
                    dimension(cad::SkCon::Radius, id, r, cad::Vec2(0.7071, 0.7071) * (r + 3), "arc radius");
            } else if(type == "point") {
                const cad::Vec2 p = vec2Arg(e.at("at"), "at");
                created.push_back({{"type", "point"}, {"id", sk.addPoint(p.x, p.y, construction)}});
            } else if(type == "text") {
                const cad::Vec2 p = vec2Arg(e.at("at"), "at");
                const std::string text = e.value("text", "");
                if(text.find_first_not_of(" \t\r\n") == std::string::npos) fail("a text entity needs its text");
                const int id = sk.addText(p, text, construction);
                cad::SkEntity &t = *sk.find(id);
                t.font = e.value("font", std::string("DejaVu Sans"));
                t.bold = e.value("bold", false);
                t.italic = e.value("italic", false);
                t.mirror = e.value("mirror", false);
                t.size = e.value("size", 5.0);
                t.angle = e.value("angle", 0.0);
                if(t.size <= 0) fail("the text size must be more than 0");
                const cad::SketchTextLetters letters = cad::sketchTextLetters(sk, t);
                if(!letters.ok) fail(letters.error);
                json made = {{"type", "text"}, {"id", id}, {"origin_point", t.a}, {"letter_regions", letters.pieces.size()}};
                if(!letters.warning.empty()) made["warning"] = letters.warning;
                created.push_back(made);
            } else if(type == "slot") {
                const cad::Vec2 a = vec2Arg(e.at("start"), "start"), b = vec2Arg(e.at("end"), "end");
                const double width = e.at("width").get<double>();
                const int c1 = sk.addPoint(a.x, a.y), c2 = sk.addPoint(b.x, b.y);
                cad::SlotIds ids;
                std::string why;
                if(!cad::addSlot(sk, c1, c2, width, ids, why)) fail(why);
                created.push_back({{"type", "slot"}, {"lines", {ids.line1, ids.line2}}, {"arcs", {ids.arc1, ids.arc2}},
                                   {"centre_line", ids.centreLine}});
                if(e.value("dimension", true)) {
                    const cad::Vec2 n = (b - a).normalized().perp();
                    dimension(cad::SkCon::Distance, ids.centreLine, (b - a).length(), n * (width / 2 + 3), "slot length");
                    dimension(cad::SkCon::Diameter, ids.arc2, width, (b - a).normalized() * (width / 2 + 3), "slot width");
                }
            } else if(type == "regular_polygon") {
                const cad::Vec2 c = vec2Arg(e.at("center"), "center");
                const int sides = e.value("sides", 6);
                const double dia = e.at("diameter").get<double>();
                const bool inscribed = e.value("inscribed", true);
                const double ang = e.value("angle", 0.0) * cad::kPi / 180.0;
                if(dia <= 0) fail("a polygon needs a positive diameter");
                const int centre = sk.addPoint(c.x, c.y);
                cad::PolygonIds ids;
                std::string why;
                const cad::Vec2 toward = c + cad::Vec2(std::cos(ang), std::sin(ang)) * (dia / 2);
                if(!cad::addRegularPolygon(sk, centre, toward, sides, inscribed, ids, why)) fail(why);
                created.push_back({{"type", "regular_polygon"}, {"lines", ids.lines}, {"circle", ids.circle},
                                   {"center_point", centre}});
                if(e.value("dimension", true))
                    dimension(cad::SkCon::Diameter, ids.circle, dia, cad::Vec2(0.7071, 0.7071) * (dia / 2 + 3),
                              inscribed ? "polygon diameter (corners)" : "polygon width across flats");
            } else {
                fail("unknown entity type \"" + type + "\" (rectangle, center_rectangle, circle, line, polygon, polyline, arc, point, text, slot, regular_polygon)");
            }
        }
    };

    const json entitiesSchema = arrayOf(
        {{"type", "object"},
         {"description",
          "One of: {type:\"rectangle\", corner1:[x,y], corner2:[x,y]}, {type:\"center_rectangle\", center:[x,y], width, height}, "
          "{type:\"circle\", center:[x,y], diameter | radius}, {type:\"line\", from:[x,y], to:[x,y]}, "
          "{type:\"polygon\", points:[[x,y],...]} (closed; \"polyline\" is open), {type:\"arc\", center, start, end} "
          "(counter-clockwise from start), {type:\"point\", at:[x,y]}, {type:\"text\", at:[x,y] (start of the first "
          "line's baseline), text (\\n for more lines), size (letter height, mm, default 5), font (default \"DejaVu "
          "Sans\"; also \"DejaVu Serif\", \"DejaVu Sans Mono\" or an installed font), bold, italic, angle (degrees), "
          "mirror (bool: letters reversed, to read from the other side)}: every letter becomes a region to extrude; "
          "{type:\"slot\", start:[x,y], end:[x,y] (the end centres), width}; {type:\"regular_polygon\", center:[x,y], "
          "sides (default 6), diameter, inscribed (default true: corners on the diameter; false: diameter is the width "
          "across flats, as for a nut trap), angle (degrees, of the first corner or side middle)}. Optional: "
          "construction (bool), dimension (bool: "
          "add driving dimensions that become editable parameters; default true for rectangles, circles, slots and polygons)."}},
        "Sketch geometry in the sketch's own 2D coordinates (mm). On XY these are world X, Y; on XZ, world X and Z; on "
        "YZ, world Y and Z; on a face or construction plane, see the frame this tool returns.");

    // --- inspect -----------------------------------------------------------------------------
    add("get_design",
        "The open design: timeline features (id, type, status, parameters), bodies (volume, area, bounding box, face and "
        "edge counts), sketches with their profiles, construction planes, what is visible (and the browser folders), "
        "parameters, and the history marker. Call this first and after edits.",
        nullptr, {}, [this, &doc, settle, bodiesJson, sketchJson](const json &) {
            const cad::StatePtr st = settle();
            const cad::ParamTable &params = doc.params();
            json features = json::array();
            for(size_t i = 0; i < doc.features().size(); ++i) {
                const cad::FeaturePtr &f = doc.features()[i];
                const cad::Status s = doc.statusOf(f->id);
                json ps = json::array();
                for(const cad::ParamDef &d : f->params()) {
                    json p = {{"name", d.name}, {"expr", d.expr}, {"label", d.label}};
                    if(const cad::ParamValue *v = params.find(d.name); v && v->ok)
                        p["value"] = d.kind == cad::ValueKind::Angle ? r3(v->value * 180.0 / M_PI) : r3(v->value);
                    ps.push_back(p);
                }
                json fj = {{"id", f->id},         {"name", f->name},
                           {"type", cad::toString(f->type())}, {"suppressed", f->suppressed},
                           {"active", int(i) < doc.marker()},  {"status", severity(s.severity)},
                           {"parameters", ps}};
                if(!s.message.empty()) fj["message"] = s.message;
                if(!f->dependencies().empty()) fj["uses"] = f->dependencies();
                features.push_back(fj);
            }
            json sketches = json::array();
            for(const auto &[id, sk] : st->sketches) {
                json s = sketchJson(*sk);
                s["id"] = id;
                s["name"] = sk->name;
                s["visible"] = m_w.modelView()->sketchShown(id);
                sketches.push_back(s);
            }
            json planes = json::array();
            for(const auto &[id, p] : st->planes)
                planes.push_back({{"id", id}, {"name", p->name}, {"origin", pt(p->frame.Location())},
                                  {"center", pt(p->center)}, {"normal", dir(p->frame.Direction())},
                                  {"visible", doc.planeVisible(id) && doc.folderVisible("construction")}});
            json folders = json::object();
            for(const std::string &f : cad::Document::folderNames()) folders[f] = doc.folderVisible(f);
            return json{{"units", "mm, degrees; Z is up (print direction)"},
                        {"features", features},
                        {"marker", doc.marker()},
                        {"bodies", bodiesJson(st)},
                        {"sketches", sketches},
                        {"construction_planes", planes},
                        {"folders", folders},
                        {"render", renderJson(doc.renderSettings())},
                        {"can_undo", doc.canUndo()},
                        {"undo", doc.undoLabel()}};
        });

    add("list_faces",
        "Faces of a body with an index for other tools: type (plane, cylinder...), area, centre, outward normal of planes, "
        "radius and axis of cylinders. Filter with normal (\"+z\", \"-x\"...) or type; sorted by distance to near.",
        {{"body", str("body id or name")},
         {"normal", str("only planar faces facing this way: +x, -x, +y, -y, +z, -z")},
         {"type", enumOf({"plane", "cylinder", "cone", "sphere", "torus", "other"}, "only faces of this type")},
         {"near", xyz("sort by distance from this point")},
         {"limit", integer("at most this many (default 100)")}},
        {"body"}, [settle, bodyOf](const json &a) {
            const cad::StatePtr st = settle();
            const cad::Body *b = bodyOf(st, a.at("body"));
            const cad::KernelLock lock(cad::kernelMutex());
            std::vector<std::pair<double, json>> out;
            const std::string want = lower(a.value("normal", ""));
            gp_Dir wantDir(0, 0, 1);
            if(!want.empty()) {
                if(want.size() != 2 || (want[0] != '+' && want[0] != '-') || want[1] < 'x' || want[1] > 'z')
                    fail("normal must be one of +x, -x, +y, -y, +z, -z");
                const double s = want[0] == '-' ? -1 : 1;
                wantDir = gp_Dir(want[1] == 'x' ? s : 0, want[1] == 'y' ? s : 0, want[1] == 'z' ? s : 0);
            }
            const std::optional<gp_Pnt> near = a.contains("near") ? std::optional<gp_Pnt>(pntArg(a["near"], "near")) : std::nullopt;
            for(int i = 1; i <= b->shape.faceCount(); ++i) {
                const TopoDS_Face f = b->shape.face(i);
                BRepAdaptor_Surface s(f, false);
                GProp_GProps props;
                BRepGProp::SurfaceProperties(f, props);
                std::string type = "other";
                json j = {{"index", i}, {"area_mm2", r3(props.Mass())}, {"center", pt(props.CentreOfMass())}};
                switch(s.GetType()) {
                case GeomAbs_Plane: {
                    type = "plane";
                    gp_Pln pln;
                    cad::planeOfFace(f, pln);
                    j["normal"] = dir(pln.Axis().Direction());
                    if(!want.empty() && !pln.Axis().Direction().IsEqual(wantDir, 1e-6)) continue;
                    break;
                }
                case GeomAbs_Cylinder:
                    type = "cylinder";
                    j["radius"] = r3(s.Cylinder().Radius());
                    j["axis"] = dir(s.Cylinder().Axis().Direction());
                    j["axis_point"] = pt(s.Cylinder().Location());
                    break;
                case GeomAbs_Cone: type = "cone"; break;
                case GeomAbs_Sphere: type = "sphere"; break;
                case GeomAbs_Torus: type = "torus"; break;
                default: break;
                }
                if(!want.empty() && type != "plane") continue;
                if(a.contains("type") && a["type"].get<std::string>() != type) continue;
                j["type"] = type;
                const double d = near ? near->Distance(props.CentreOfMass()) : 0.0;
                if(near) j["distance"] = r3(d);
                out.push_back({d, j});
            }
            if(near) std::stable_sort(out.begin(), out.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
            const size_t limit = size_t(std::max(1, a.value("limit", 100)));
            json list = json::array();
            for(size_t i = 0; i < out.size() && i < limit; ++i) list.push_back(out[i].second);
            return json{{"body", b->id}, {"face_count", b->shape.faceCount()}, {"faces", list}};
        });

    add("list_edges",
        "Edges of a body with an index for fillet / chamfer: type (line, circle...), length, start, end, midpoint, "
        "direction of lines, centre and radius of circles. Filter with direction (\"x\", \"y\", \"z\": lines parallel to "
        "it), type, or sort by distance from near.",
        {{"body", str("body id or name")},
         {"direction", enumOf({"x", "y", "z"}, "only straight edges parallel to this axis")},
         {"type", enumOf({"line", "circle", "other"}, "only edges of this type")},
         {"near", xyz("sort by distance from this point (to the edge's midpoint)")},
         {"limit", integer("at most this many (default 100)")}},
        {"body"}, [settle, bodyOf](const json &a) {
            const cad::StatePtr st = settle();
            const cad::Body *b = bodyOf(st, a.at("body"));
            const cad::KernelLock lock(cad::kernelMutex());
            const std::optional<gp_Pnt> near = a.contains("near") ? std::optional<gp_Pnt>(pntArg(a["near"], "near")) : std::nullopt;
            const std::string axis = a.value("direction", "");
            std::vector<std::pair<double, json>> out;
            for(int i = 1; i <= b->shape.edgeCount(); ++i) {
                const TopoDS_Edge e = b->shape.edge(i);
                BRepAdaptor_Curve c(e);
                GProp_GProps props;
                BRepGProp::LinearProperties(e, props);
                const double t0 = c.FirstParameter(), t1 = c.LastParameter();
                const gp_Pnt mid = c.Value((t0 + t1) / 2);
                json j = {{"index", i}, {"length", r3(props.Mass())}, {"start", pt(c.Value(t0))}, {"end", pt(c.Value(t1))}, {"midpoint", pt(mid)}};
                std::string type = "other";
                if(c.GetType() == GeomAbs_Line) {
                    type = "line";
                    const gp_Dir d = c.Line().Direction();
                    j["direction"] = dir(d);
                    if(!axis.empty()) {
                        const gp_Dir w(axis == "x" ? 1 : 0, axis == "y" ? 1 : 0, axis == "z" ? 1 : 0);
                        if(!d.IsParallel(w, 1e-6)) continue;
                    }
                } else if(c.GetType() == GeomAbs_Circle) {
                    type = "circle";
                    j["center"] = pt(c.Circle().Location());
                    j["radius"] = r3(c.Circle().Radius());
                    j["axis"] = dir(c.Circle().Axis().Direction());
                }
                if(!axis.empty() && type != "line") continue;
                if(a.contains("type") && a["type"].get<std::string>() != type) continue;
                j["type"] = type;
                const double d = near ? near->Distance(mid) : 0.0;
                if(near) j["distance"] = r3(d);
                out.push_back({d, j});
            }
            if(near) std::stable_sort(out.begin(), out.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
            const size_t limit = size_t(std::max(1, a.value("limit", 100)));
            json list = json::array();
            for(size_t i = 0; i < out.size() && i < limit; ++i) list.push_back(out[i].second);
            return json{{"body", b->id}, {"edge_count", b->shape.edgeCount()}, {"edges", list}};
        });

    add("screenshot",
        "A PNG picture of the canvas, optionally from a standard view and in a display style. Use it to check the result.",
        {{"view", enumOf({"home", "front", "back", "left", "right", "top", "bottom"}, "look from here first (and fit)")},
         {"style", enumOf({"shaded_edges", "shaded", "wireframe", "rendered"}, "display style")},
         {"max_width", integer("scale down to at most this width in pixels (default 1200)")}},
        {}, [this, settle](const json &a) {
            settle();
            Viewport *vp = m_w.viewport();
            if(a.contains("view")) {
                const std::string v = a["view"];
                const std::map<std::string, StandardView> views = {{"home", StandardView::Home}, {"front", StandardView::Front},
                                                                   {"back", StandardView::Back}, {"left", StandardView::Left},
                                                                   {"right", StandardView::Right}, {"top", StandardView::Top},
                                                                   {"bottom", StandardView::Bottom}};
                if(!views.count(v)) fail("unknown view " + v);
                vp->setStandardView(views.at(v), false);
                vp->fitAll(false);
            }
            if(a.contains("style")) {
                const std::map<std::string, DisplayStyle> styles = {{"shaded_edges", DisplayStyle::ShadedWithEdges},
                                                                    {"shaded", DisplayStyle::Shaded},
                                                                    {"wireframe", DisplayStyle::Wireframe},
                                                                    {"rendered", DisplayStyle::Rendered}};
                const std::string s = a["style"];
                if(!styles.count(s)) fail("unknown style " + s);
                vp->setDisplayStyle(styles.at(s));
            }
            waitForFrames(vp, 2);
            QImage img = vp->grabFramebuffer();
            if(img.isNull()) fail("the canvas could not be captured");
            const int maxW = std::max(200, a.value("max_width", 1200));
            if(img.width() > maxW) img = img.scaledToWidth(maxW, Qt::SmoothTransformation);
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            return json{{"content",
                         json::array({{{"type", "image"}, {"data", png.toBase64().toStdString()}, {"mimeType", "image/png"}},
                                      {{"type", "text"},
                                       {"text", "Canvas " + std::to_string(img.width()) + " x " + std::to_string(img.height()) + " px"}}})},
                        {"isError", false}};
        });

    // --- sketches -----------------------------------------------------------------------------
    add("create_sketch",
        "Creates a sketch (one timeline step) on an origin plane, a construction plane or a planar face, with its "
        "geometry. Returns the sketch id, its frame (for converting between sketch and world coordinates), its closed "
        "profiles (regions to extrude, each with an inside point) and the parameters of its dimensions.",
        {{"plane", planeSpec()}, {"entities", entitiesSchema}, {"name", str("optional name, e.g. \"Base outline\"")}},
        {"plane", "entities"}, [&doc, begin, settle, planeOf, addEntities, sketchResultOf, sketchJson](const json &a) {
            begin();
            const cad::StatePtr before = settle();
            auto sf = std::make_shared<cad::SketchFeature>();
            sf->plane = planeOf(before, a.at("plane"));
            json created = json::array(), params = json::array();
            addEntities(sf->sketch, a.at("entities"), created, params);
            const cad::FeatureId id = doc.addFeature(sf, "Create Sketch (MCP)");
            if(a.contains("name")) doc.renameFeature(id, a["name"].get<std::string>());
            const cad::StatePtr st = settle();
            const cad::Status s = doc.statusOf(id);
            if(s.isError()) {
                doc.undo();
                settle();
                fail(s.message + " (nothing was added)");
            }
            json r = sketchJson(*sketchResultOf(st, id));
            r["sketch"] = id;
            r["name"] = doc.feature(id)->name;
            r["entities"] = created;
            r["parameters"] = params;
            return r;
        });

    add("add_to_sketch",
        "Adds geometry to an existing sketch (one undo step). Bodies already made from the sketch keep their shape; the "
        "new regions can be extruded as new bodies.",
        {{"sketch", integer("sketch feature id")}, {"entities", entitiesSchema}}, {"sketch", "entities"},
        [&doc, begin, settle, featureOf, addEntities, sketchResultOf, sketchJson](const json &a) {
            begin();
            const cad::FeaturePtr f = featureOf(a.at("sketch"));
            if(f->type() != cad::FeatureType::Sketch) fail("feature " + f->name + " is not a sketch");
            auto sf = std::static_pointer_cast<cad::SketchFeature>(f->clone());
            json created = json::array(), params = json::array();
            addEntities(sf->sketch, a.at("entities"), created, params);
            doc.replaceFeature(sf, "Edit " + f->name + " (MCP)");
            const cad::StatePtr st = settle();
            json r = sketchJson(*sketchResultOf(st, f->id));
            r["sketch"] = f->id;
            r["entities"] = created;
            r["parameters"] = params;
            return r;
        });

    // Sketch Mirror, Circular Pattern, Project and construction geometry.
    auto editSketch = [&doc, begin, settle, featureOf, sketchResultOf, sketchJson](
                          const json &a, const std::string &label,
                          const std::function<void(cad::Sketch &, const cad::SketchResult &, const cad::StatePtr &)> &fn) {
        begin();
        const cad::FeaturePtr f = featureOf(a.at("sketch"));
        if(f->type() != cad::FeatureType::Sketch) fail("feature " + f->name + " is not a sketch");
        const cad::StatePtr before = settle();
        auto sf = std::static_pointer_cast<cad::SketchFeature>(f->clone());
        fn(sf->sketch, *sketchResultOf(before, f->id), before);
        doc.replaceFeature(sf, label + " in " + f->name + " (MCP)");
        const cad::StatePtr st = settle();
        json r = sketchJson(*sketchResultOf(st, f->id));
        r["sketch"] = f->id;
        return r;
    };
    auto entityIds = [](const json &a) {
        std::vector<int> ids;
        for(const json &v : a.value("entities", json::array())) {
            if(!v.is_number_integer()) fail("entities are entity ids (integers; see get_design's sketch curves)");
            ids.push_back(v.get<int>());
        }
        if(ids.empty()) fail("give the entities (ids from the sketch's curves)");
        return ids;
    };
    auto solvedOrFail = [](cad::Sketch &s, const std::string &what) {
        cad::Sketch probe = s;
        const cad::SolveOutcome r = cad::solveSketch(probe, [](const std::string &, double &) { return false; });
        if(!r.ok) fail(what + ": " + r.message);
    };

    add("sketch_mirror",
        "Mirrors sketch geometry about a line of the sketch (or its x / y axis), like the sketch Mirror tool: the "
        "copies stay symmetric to the originals. Points on the mirror line are shared, so half an outline drawn up "
        "to the line becomes one closed profile. One undo step. Returns the sketch's curves and profiles.",
        {{"sketch", integer("sketch feature id")},
         {"entities", arrayOf(integer("entity id"), "the curves / points / texts to mirror (ids from the sketch's curves)")},
         {"line", {{"anyOf", json::array({{{"type", "integer"}}, {{"type", "string"}, {"enum", {"x_axis", "y_axis"}}}})},
                   {"description", "the line to mirror about: a line entity id, \"x_axis\" or \"y_axis\""}}}},
        {"sketch", "entities", "line"}, [editSketch, entityIds, solvedOrFail](const json &a) {
            const std::vector<int> ids = entityIds(a);
            int line = 0;
            if(a.at("line").is_string()) {
                const std::string l = a["line"].get<std::string>();
                line = l == "x_axis" ? cad::kSketchXAxis : l == "y_axis" ? cad::kSketchYAxis : 0;
                if(!line) fail("line must be a line id, \"x_axis\" or \"y_axis\"");
            } else {
                line = a["line"].get<int>();
            }
            json made;
            json r = editSketch(a, "Mirror", [&](cad::Sketch &s, const cad::SketchResult &, const cad::StatePtr &) {
                std::vector<int> created;
                std::string why;
                if(!cad::mirrorEntities(s, ids, line, created, why)) fail(why);
                solvedOrFail(s, "the mirror image cannot be solved");
                made = created;
            });
            r["created"] = made;
            return r;
        });

    add("sketch_pattern",
        "Circular pattern in a sketch, like the sketch Circular Pattern tool: copies of geometry spread evenly around "
        "a centre point. `count` is how many in all (the original included); `angle` the angle they spread over "
        "(default 360: all the way round). Dimensions of the copies follow the original's. One undo step.",
        {{"sketch", integer("sketch feature id")},
         {"entities", arrayOf(integer("entity id"), "the curves / points / texts to copy")},
         {"center", xy("the centre (sketch coordinates)")},
         {"count", integer("how many in all, 2-360")},
         {"angle", number("degrees, default 360")}},
        {"sketch", "entities", "center", "count"}, [editSketch, entityIds, solvedOrFail](const json &a) {
            const std::vector<int> ids = entityIds(a);
            const cad::Vec2 c = vec2Arg(a.at("center"), "center");
            const int count = a.at("count").get<int>();
            const double angle = a.value("angle", 360.0);
            json made;
            json r = editSketch(a, "Circular Pattern", [&](cad::Sketch &s, const cad::SketchResult &, const cad::StatePtr &) {
                std::vector<int> created;
                std::string why;
                if(!cad::patternEntities(s, ids, c, count, angle, created, why)) fail(why);
                solvedOrFail(s, "the pattern cannot be solved");
                made = created;
            });
            r["created"] = made;
            return r;
        });

    add("project_to_sketch",
        "Projects lines, curves and points of another sketch (usually on another plane) onto a sketch, like the "
        "sketch Project tool. Projected geometry stays linked: when the original changes, it follows. It is fixed in "
        "this sketch, can bound profiles like any line, and can be made construction geometry (set_construction). "
        "Circles and arcs project only onto a parallel plane. The other sketch must come before this one in the "
        "timeline. One undo step.",
        {{"sketch", integer("the sketch to project onto")},
         {"from_sketch", integer("the sketch to project from")},
         {"entities", arrayOf(integer("entity id"), "what to project (default: all its curves)")}},
        {"sketch", "from_sketch"}, [&doc, editSketch, featureOf](const json &a) {
            const cad::FeaturePtr from = featureOf(a.at("from_sketch"));
            const cad::FeaturePtr to = featureOf(a.at("sketch"));
            if(from->type() != cad::FeatureType::Sketch) fail("feature " + from->name + " is not a sketch");
            if(doc.indexOf(from->id) >= doc.indexOf(to->id))
                fail(from->name + " comes after " + to->name + " in the timeline: project from an earlier sketch");
            json made = json::array();
            json r = editSketch(a, "Project", [&](cad::Sketch &s, const cad::SketchResult &target, const cad::StatePtr &st) {
                auto it = st->sketches.find(from->id);
                if(it == st->sketches.end()) fail(from->name + " has no result (is it suppressed?)");
                const cad::SketchResult &src = *it->second;
                std::vector<int> ids;
                if(a.contains("entities") && !a["entities"].empty()) {
                    for(const json &v : a["entities"]) ids.push_back(v.get<int>());
                } else {
                    for(const auto &e : src.sketch.entities)
                        if(e.isCurve() && cad::canProject(src, e, target.frame)) ids.push_back(e.id);
                }
                if(ids.empty()) fail("nothing to project");
                for(int id : ids) {
                    std::vector<int> created;
                    std::string why;
                    if(!cad::projectEntity(s, target.frame, src, id, created, why))
                        fail("entity " + std::to_string(id) + ": " + why);
                    for(int c : created) made.push_back(c);
                }
            });
            r["created"] = made;
            return r;
        });

    add("sketch_trim",
        "Trims a sketch curve like the sketch Trim tool: removes the piece of curve `curve` around the point `near` "
        "(sketch coordinates), between the nearest curves crossing it. A line or arc is shortened or split; a circle "
        "becomes an arc; a curve nothing crosses is deleted. One undo step.",
        {{"sketch", integer("sketch feature id")}, {"curve", integer("the curve's entity id")},
         {"near", xy("a point on the piece to remove")}},
        {"sketch", "curve", "near"}, [editSketch, solvedOrFail](const json &a) {
            const int curve = a.at("curve").get<int>();
            const cad::Vec2 near = vec2Arg(a.at("near"), "near");
            json made;
            json r = editSketch(a, "Trim", [&](cad::Sketch &s, const cad::SketchResult &, const cad::StatePtr &) {
                std::vector<int> created;
                std::string why;
                if(!cad::trimCurve(s, curve, near, created, why)) fail(why);
                solvedOrFail(s, "the trimmed sketch cannot be solved");
                made = created;
            });
            r["created"] = made;
            return r;
        });

    add("sketch_extend",
        "Extends a line or arc like the sketch Extend tool: its end nearer `near` is lengthened up to the next curve "
        "in that direction and held on it. The end must be free. One undo step.",
        {{"sketch", integer("sketch feature id")}, {"curve", integer("the line or arc's entity id")},
         {"near", xy("a point near the end to extend")}},
        {"sketch", "curve", "near"}, [editSketch, solvedOrFail](const json &a) {
            const int curve = a.at("curve").get<int>();
            const cad::Vec2 near = vec2Arg(a.at("near"), "near");
            return editSketch(a, "Extend", [&](cad::Sketch &s, const cad::SketchResult &, const cad::StatePtr &) {
                std::string why;
                if(!cad::extendCurve(s, curve, near, why)) fail(why);
                solvedOrFail(s, "the extended sketch cannot be solved");
            });
        });

    add("sketch_fillet",
        "Rounds a sketch corner where two lines meet with a tangent arc of `radius`, like the sketch Fillet tool. "
        "Give the corner point's id (`corner`) or a point `near` it. The corner stays as a construction point, so "
        "the lines' dimensions still measure to it; the radius becomes a parameter. One undo step.",
        {{"sketch", integer("sketch feature id")}, {"corner", integer("the corner point's entity id")},
         {"near", xy("or: a point near the corner")}, {"radius", number("mm")}},
        {"sketch", "radius"}, [&doc, editSketch, solvedOrFail](const json &a) {
            const double radius = a.at("radius").get<double>();
            json made;
            json r = editSketch(a, "Fillet", [&](cad::Sketch &s, const cad::SketchResult &, const cad::StatePtr &) {
                int corner = a.value("corner", 0);
                if(!corner && a.contains("near")) corner = cad::filletCornerNear(s, vec2Arg(a["near"], "near"), 1e9);
                if(!corner) fail("give the corner (a point where two lines meet) or a point near it");
                int arc = 0;
                std::vector<int> created;
                std::string why;
                if(!cad::filletCorner(s, corner, radius, arc, created, why)) fail(why);
                const int cid = s.addConstraint(cad::SkCon::Radius, arc);
                cad::SkConstraint *c = s.findConstraint(cid);
                c->param = doc.allocateParamName();
                c->expr = num(radius) + " mm";
                solvedOrFail(s, "the filleted sketch cannot be solved");
                made = {{"arc", arc}, {"radius_parameter", c->param}};
            });
            r["created"] = made;
            return r;
        });

    add("set_construction",
        "Makes sketch geometry construction geometry (dotted, a reference for drawing: it bounds no profile and is "
        "never extruded) or normal again. One undo step.",
        {{"sketch", integer("sketch feature id")},
         {"entities", arrayOf(integer("entity id"), "the curves / points")},
         {"construction", boolean("true: construction (default); false: normal")}},
        {"sketch", "entities"}, [editSketch, entityIds](const json &a) {
            const std::vector<int> ids = entityIds(a);
            const bool on = a.value("construction", true);
            return editSketch(a, on ? "Construction" : "Normal", [&](cad::Sketch &s, const cad::SketchResult &, const cad::StatePtr &) {
                for(int id : ids) {
                    cad::SkEntity *e = s.find(id);
                    if(!e) fail("no entity " + std::to_string(id) + " in this sketch");
                    e->construction = on;
                }
            });
        });

    add("offset_sketch",
        "Offset (like Fusion's sketch Offset): copies sketch curves a distance to one side, joined up at the corners, "
        "held there by ONE new offset dimension (a parameter you can change later). Pick curves by entity id "
        "(`curves`, e.g. from add_to_sketch) or by points on or near them (`near`, sketch coordinates); with `chain` "
        "(default) each picks everything joined to it end to end, so one point on a rectangle takes the whole outline. "
        "Shell a part: offset its outline inwards by the wall thickness, then extrude the ring between the two. "
        "Clearances for lids and fits work the same way (e.g. 0.2 mm).",
        {{"sketch", integer("sketch feature id")},
         {"curves", arrayOf(integer("entity id"), "the curves to offset")},
         {"near", arrayOf(xy("a point on or near the curve"), "the curves nearest these points")},
         {"distance", {{"type", "number"},
                       {"description", "mm. Positive: closed outlines grow outwards (open curves go to the side of "
                                       "`side_point`, else to the left of the first curve's direction); negative: "
                                       "the other way"}}},
         {"side_point", xy("optional: a point on the side to offset to (the sign of distance is then ignored)")},
         {"chain", boolean("pick whole chains of joined curves (default true)")}},
        {"sketch", "distance"},
        [&doc, begin, settle, featureOf, sketchResultOf, sketchJson](const json &a) {
            begin();
            const cad::FeaturePtr f = featureOf(a.at("sketch"));
            if(f->type() != cad::FeatureType::Sketch) fail("feature " + f->name + " is not a sketch");
            auto sf = std::static_pointer_cast<cad::SketchFeature>(f->clone());
            cad::Sketch &sk = sf->sketch;
            const bool chain = a.value("chain", true);
            std::vector<int> ids;
            auto take = [&](int id) {
                const std::vector<int> add = chain ? cad::connectedCurves(sk, id) : std::vector<int>{id};
                for(int c : add)
                    if(std::find(ids.begin(), ids.end(), c) == ids.end()) ids.push_back(c);
            };
            for(const json &c : a.value("curves", json::array())) {
                const int id = c.get<int>();
                const cad::SkEntity *e = sk.find(id);
                if(!e || !e->isCurve()) fail("sketch entity " + std::to_string(id) + " is not a line, arc or circle");
                take(id);
            }
            for(const json &p : a.value("near", json::array())) {
                const int id = cad::nearestCurve(sk, vec2Arg(p, "near"));
                if(!id) fail("the sketch has no curves");
                take(id);
            }
            if(ids.empty()) fail("give curves or near points to pick what to offset");
            std::vector<cad::CurveChain> chains;
            std::string why;
            if(!cad::buildChains(sk, ids, chains, why)) fail(why);
            const double distance = a.at("distance").get<double>();
            if(std::fabs(distance) < 1e-9) fail("distance cannot be 0");
            double left;
            if(a.contains("side_point")) {
                const double side = cad::sideDistance(sk, chains, vec2Arg(a["side_point"], "side_point"));
                left = side < 0 ? -std::fabs(distance) : std::fabs(distance);
            } else {
                left = distance * cad::outwardSign(sk, chains.front());
            }
            const std::string param = doc.allocateParamName();
            const std::string expr = num(std::fabs(distance)) + " mm";
            const cad::ParamTable &table = doc.params();
            const cad::DimensionLookup lookup = [&table](const std::string &name, double &v) {
                const cad::ParamValue *pv = table.find(name);
                if(!pv || !pv->ok) return false;
                v = pv->value;
                return true;
            };
            cad::OffsetApplied applied;
            if(!cad::applyOffset(sk, chains, left, param, expr, lookup, applied, why)) fail(why);
            doc.replaceFeature(sf, "Offset in " + f->name + " (MCP)");
            const cad::StatePtr st = settle();
            json r = sketchJson(*sketchResultOf(st, f->id));
            r["sketch"] = f->id;
            json made = json::array();
            for(int id : applied.entities)
                if(const cad::SkEntity *e = sk.find(id); e && e->isCurve()) made.push_back(id);
            r["offset_curves"] = made;
            r["source_curves"] = ids;
            if(applied.dimension) r["parameter"] = {{"name", param}, {"expr", expr}, {"measures", "offset distance"}};
            return r;
        });

    // --- features -------------------------------------------------------------------------------
    add("extrude",
        "Extrudes sketch profiles (regions) or planar faces into a solid. By default it makes a NEW BODY (combine bodies "
        "afterwards with combine). Profiles are picked by points inside them (sketch coordinates); without points every "
        "profile of the sketch is used.",
        {{"sketch", integer("sketch feature id (for profiles)")},
         {"profile_points", arrayOf(xy("a point inside the region"), "which regions of the sketch (sketch coordinates)")},
         {"faces", arrayOf(topoItem("face"), "planar faces to extrude instead of / as well as profiles")},
         {"distance", numberOrExpr("length in mm (negative: the other way), default 10")},
         {"direction", enumOf({"one_side", "two_sides", "symmetric"}, "default one_side")},
         {"distance2", numberOrExpr("second side length for two_sides")},
         {"extent", enumOf({"distance", "all"}, "all: through everything (default distance)")},
         {"flip", boolean("with extent all: go the other way")},
         {"taper", numberOrExpr("taper angle in degrees (positive flares outwards)")},
         {"operation", enumOf({"new_body", "join", "cut", "intersect"}, "default new_body")}},
        {}, [&doc, begin, settle, topo, commit, sketchResultOf, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto e = std::make_shared<cad::ExtrudeFeature>();
            if(a.contains("sketch")) {
                const cad::FeatureId sid = a["sketch"].get<int>();
                const cad::SketchResult *sk = sketchResultOf(st, sid);
                if(sk->profiles.empty()) fail("the sketch has no closed profiles");
                auto addProfile = [&](const cad::Profile &p) {
                    cad::ProfileRef r{sid, p.key, p.sample};
                    cad::captureProfileOutline(*st, r);
                    e->profiles.push_back(r);
                };
                if(a.contains("profile_points")) {
                    for(const json &q : a["profile_points"]) {
                        const cad::Vec2 p = vec2Arg(q, "profile point");
                        const cad::Profile *hit = nullptr;
                        for(const auto &pr : sk->profiles)
                            if(pr.contains(p) && (!hit || std::fabs(pr.area) < std::fabs(hit->area))) hit = &pr;
                        if(!hit) fail("no profile of the sketch contains [" + num(p.x) + ", " + num(p.y) + "]");
                        addProfile(*hit);
                    }
                } else {
                    for(const auto &pr : sk->profiles) addProfile(pr);
                }
            }
            for(const json &f : a.value("faces", json::array())) e->faces.push_back(topo(st, f, cad::TopoKind::Face));
            if(e->profiles.empty() && e->faces.empty()) fail("give a sketch (with profile_points) or faces to extrude");
            const std::string dirn = a.value("direction", "one_side");
            e->direction = dirn == "two_sides" ? cad::ExtrudeDirection::TwoSides
                           : dirn == "symmetric" ? cad::ExtrudeDirection::Symmetric
                                                 : cad::ExtrudeDirection::OneSide;
            e->extent = a.value("extent", "distance") == "all" ? cad::ExtentType::ThroughAll : cad::ExtentType::Distance;
            e->extent2 = cad::ExtentType::Distance;
            e->distance = slot(lengthExpr(a.value("distance", json(10.0)), "distance"), cad::ValueKind::Length, "distance");
            if(e->direction == cad::ExtrudeDirection::TwoSides)
                e->distance2 = slot(lengthExpr(a.value("distance2", json(10.0)), "distance2"), cad::ValueKind::Length, "distance2");
            if(a.contains("taper")) e->taper = slot(angleExpr(a["taper"], "taper"), cad::ValueKind::Angle, "taper");
            e->flip = a.value("flip", false);
            const std::string op = a.value("operation", "new_body");
            e->operation = op == "join" ? cad::BodyOperation::Join
                           : op == "cut" ? cad::BodyOperation::Cut
                           : op == "intersect" ? cad::BodyOperation::Intersect
                                               : cad::BodyOperation::NewBody;
            return commit(e, "Create Extrude (MCP)");
        });

    add("fillet", "Rounds edges (from list_edges) with a radius. Faces round all their edges.",
        {{"edges", arrayOf(topoItem("edge"), "edges to round")},
         {"faces", arrayOf(topoItem("face"), "faces whose edges to round")},
         {"radius", numberOrExpr("radius in mm")}},
        {"radius"}, [begin, settle, topo, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto f = std::make_shared<cad::FilletFeature>();
            for(const json &e : a.value("edges", json::array())) f->edges.push_back(topo(st, e, cad::TopoKind::Edge));
            for(const json &e : a.value("faces", json::array())) f->faces.push_back(topo(st, e, cad::TopoKind::Face));
            if(f->edges.empty() && f->faces.empty()) fail("give edges or faces to fillet");
            f->radius = slot(lengthExpr(a.at("radius"), "radius"), cad::ValueKind::Length, "radius");
            return commit(f, "Create Fillet (MCP)");
        });

    add("chamfer",
        "Bevels edges (from list_edges): equal distance, two distances, or distance and angle. Chamfers on bottom edges "
        "print better than fillets (no overhang).",
        {{"edges", arrayOf(topoItem("edge"), "edges to chamfer")},
         {"faces", arrayOf(topoItem("face"), "faces whose edges to chamfer")},
         {"distance", numberOrExpr("distance in mm")},
         {"type", enumOf({"equal", "two_distances", "distance_angle"}, "default equal")},
         {"distance2", numberOrExpr("second distance (two_distances)")},
         {"angle", numberOrExpr("angle in degrees (distance_angle), default 45")},
         {"flip", boolean("swap the sides of distance / angle")}},
        {"distance"}, [begin, settle, topo, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto c = std::make_shared<cad::ChamferFeature>();
            for(const json &e : a.value("edges", json::array())) c->edges.push_back(topo(st, e, cad::TopoKind::Edge));
            for(const json &e : a.value("faces", json::array())) c->faces.push_back(topo(st, e, cad::TopoKind::Face));
            if(c->edges.empty() && c->faces.empty()) fail("give edges or faces to chamfer");
            const std::string type = a.value("type", "equal");
            c->chamferType = type == "two_distances"    ? cad::ChamferType::TwoDistances
                             : type == "distance_angle" ? cad::ChamferType::DistanceAngle
                                                        : cad::ChamferType::EqualDistance;
            c->distance = slot(lengthExpr(a.at("distance"), "distance"), cad::ValueKind::Length, "distance");
            c->distance2 = slot(lengthExpr(a.value("distance2", a.at("distance")), "distance2"), cad::ValueKind::Length, "distance2");
            c->angle = slot(angleExpr(a.value("angle", json(45.0)), "angle"), cad::ValueKind::Angle, "angle");
            c->flip = a.value("flip", false);
            return commit(c, "Create Chamfer (MCP)");
        });

    add("hole",
        "Drills holes into a planar face at world points on it (or at the points of a sketch): simple, counterbore "
        "(for socket head screws), countersink (for flat head screws) or tapped (a threaded hole: set thread, e.g. "
        "\"M5\"; M5 and up are modelled, smaller ones left at the tap drill unless thread_mode is \"modeled\"); to a "
        "depth or through all.",
        {{"face", topoItem("face")},
         {"points", arrayOf(xyz("a point on the face"), "hole centres (world coordinates, mm)")},
         {"sketch", integer("instead of face + points: every point entity of this sketch (holes go along its normal)")},
         {"type", enumOf({"simple", "counterbore", "countersink", "tapped"}, "default simple")},
         {"thread", str("tapped: the thread size, e.g. \"M3\", \"M5\", \"M8x1\", \"1/4-20 UNC\" (see the thread tool)")},
         {"thread_mode", enumOf({"auto", "modeled", "tap_drill"}, "tapped: auto (default) models M5 / #10 and up")},
         {"thread_clearance", numberOrExpr("tapped: radial print clearance, mm (default 0.15)")},
         {"diameter", numberOrExpr("hole diameter, default 5")},
         {"depth", numberOrExpr("depth, default 10 (ignored with through_all)")},
         {"through_all", boolean("go through everything")},
         {"cbore_diameter", numberOrExpr("counterbore diameter, default 9")},
         {"cbore_depth", numberOrExpr("counterbore depth, default 3")},
         {"csink_diameter", numberOrExpr("countersink diameter, default 9")},
         {"csink_angle", numberOrExpr("countersink angle, default 90")},
         {"tip_angle", numberOrExpr("drill point angle, default 118")},
         {"flat_tip", boolean("flat bottom instead of a drill point")}},
        {}, [begin, settle, topo, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto h = std::make_shared<cad::HoleFeature>();
            if(a.contains("sketch")) {
                h->sketch = a["sketch"].get<int>();
                auto it = st->sketches.find(h->sketch);
                if(it == st->sketches.end()) fail("no sketch " + std::to_string(h->sketch));
                for(const auto &e : it->second->sketch.entities)
                    if(e.type == cad::SkType::Point && !e.construction) {
                        bool used = false;
                        for(const auto &o : it->second->sketch.entities)
                            used = used || (o.type != cad::SkType::Point && (o.a == e.id || o.b == e.id || o.c == e.id));
                        if(!used) h->sketchPoints.push_back(e.id);
                    }
                if(h->sketchPoints.empty()) fail("the sketch has no stand-alone points (add {type: \"point\"} entities)");
            } else {
                h->face = topo(st, a.at("face"), cad::TopoKind::Face);
                const cad::ResolvedRef r = cad::resolveRef(*st, h->face);
                gp_Pln pln;
                if(!r.ok || !cad::planeOfFace(TopoDS::Face(r.shape), pln)) fail("holes go on planar faces");
                const gp_Ax3 frame = cad::frameForPlane(pln);
                for(const json &p : a.at("points")) {
                    const gp_Vec v(frame.Location(), pntArg(p, "point"));
                    h->points.push_back({v.Dot(gp_Vec(frame.XDirection())), v.Dot(gp_Vec(frame.YDirection()))});
                }
                if(h->points.empty()) fail("give at least one point");
            }
            const std::string type = a.value("type", "simple");
            h->holeType = type == "counterbore"   ? cad::HoleType::Counterbore
                          : type == "countersink" ? cad::HoleType::Countersink
                          : type == "tapped"      ? cad::HoleType::Tapped
                                                  : cad::HoleType::Simple;
            if(h->holeType == cad::HoleType::Tapped) {
                h->thread = a.value("thread", "");
                const cad::ThreadSpec *spec = cad::findThread(h->thread);
                if(!spec) fail("tapped holes need a known thread size (\"M3\", \"M5\", \"1/4-20 UNC\"...)");
                h->thread = spec->name;
                const std::string m = a.value("thread_mode", "auto");
                h->threadMode = m == "modeled" ? cad::ThreadMode::Modeled : m == "tap_drill" ? cad::ThreadMode::TapDrill : cad::ThreadMode::Auto;
                h->threadClearance = slot(lengthExpr(a.value("thread_clearance", json(0.15)), "thread_clearance"),
                                          cad::ValueKind::Length, "thread_clearance");
            }
            h->extent = a.value("through_all", false) ? cad::ExtentType::ThroughAll : cad::ExtentType::Distance;
            auto len = [&](const char *key, double def) {
                return slot(lengthExpr(a.value(key, json(def)), key), cad::ValueKind::Length, key);
            };
            auto ang = [&](const char *key, double def) {
                return slot(angleExpr(a.value(key, json(def)), key), cad::ValueKind::Angle, key);
            };
            h->diameter = len("diameter", 5);
            h->depth = len("depth", 10);
            h->cboreDiameter = len("cbore_diameter", 9);
            h->cboreDepth = len("cbore_depth", 3);
            h->csinkDiameter = len("csink_diameter", 9);
            h->csinkAngle = ang("csink_angle", 90);
            h->tipAngle = ang("tip_angle", 118);
            h->flatTip = a.value("flat_tip", false);
            return commit(h, "Create Hole (MCP)");
        });

    add("emboss_text",
        "Engraves text into a face (cut in) or embosses it (raised) on any face of a body; on a curved face the letters "
        "follow the surface (they wrap exactly around cylinders and cones). The text's middle goes at `at` (a world "
        "point on or near the face) or at `position` in the face's own frame, else at the middle of the face. On a "
        "flat face the text reads from outside with y up on upright faces (on a top face, x and y are world X and Y); "
        "on a curved face y runs up along the surface. Returns the feature, the volume change and where the text is.",
        {{"face", topoItem("face")},
         {"text", str("the text; \\n for more lines")},
         {"at", xyz("where the middle of the text goes (world, mm): the nearest point of the face")},
         {"position", xy("instead of at: the middle of the text in the face's frame (mm)")},
         {"size", numberOrExpr("letter height (the font's size), mm, default 5")},
         {"depth", numberOrExpr("engrave depth or emboss height, mm, default 0.6")},
         {"direction", enumOf({"engrave", "emboss"}, "engrave (cut in, default) or emboss (raised)")},
         {"rotation", numberOrExpr("turn about the face normal, degrees anticlockwise seen from outside, default 0")},
         {"font", str("font family, default \"DejaVu Sans\" (bundled: DejaVu Sans, DejaVu Serif, DejaVu Sans Mono)")},
         {"bold", boolean("bold letters")},
         {"italic", boolean("italic letters")},
         {"letter_spacing", numberOrExpr("extra space between letters, mm, default 0")},
         {"line_spacing", numberOrExpr("line pitch as a multiple of the size, default 1.2")},
         {"mirror", boolean("reverse the letters, to read from the other side (stamps, moulds)")}},
        {"face", "text"}, [&doc, begin, settle, topo, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto f = std::make_shared<cad::TextFeature>();
            f->face = topo(st, a.at("face"), cad::TopoKind::Face);
            f->text = a.at("text").get<std::string>();
            if(f->text.find_first_not_of(" \t\r\n") == std::string::npos) fail("give the text");
            const cad::ResolvedRef r = cad::resolveRef(*st, f->face);
            if(!r.ok || r.shape.ShapeType() != TopAbs_FACE) fail("that face could not be found");
            const TopoDS_Face face = TopoDS::Face(r.shape);
            cad::FaceTextFrame frame;
            std::string why;
            if(!cad::faceTextFrame(face, frame, why)) fail(why);
            double x = 0, y = 0;
            if(a.contains("position")) {
                const cad::Vec2 p = vec2Arg(a["position"], "position");
                x = p.x;
                y = p.y;
            } else if(a.contains("at") || frame.planar) {
                // A given point, or the middle of a flat face (a curved face's frame starts at its middle).
                gp_Pnt target;
                if(a.contains("at")) {
                    target = pntArg(a["at"], "at");
                } else {
                    GProp_GProps props;
                    BRepGProp::SurfaceProperties(face, props);
                    target = props.CentreOfMass();
                }
                const cad::KernelLock lock(cad::kernelMutex());
                if(!frame.locate(target, x, y)) fail("that point could not be found on the face");
            }
            f->x = doc.makeSlot(num(std::round(x * 1000) / 1000) + " mm");
            f->y = doc.makeSlot(num(std::round(y * 1000) / 1000) + " mm");
            f->size = slot(lengthExpr(a.value("size", json(5.0)), "size"), cad::ValueKind::Length, "size");
            f->depth = slot(lengthExpr(a.value("depth", json(0.6)), "depth"), cad::ValueKind::Length, "depth");
            f->rotation = slot(angleExpr(a.value("rotation", json(0.0)), "rotation"), cad::ValueKind::Angle, "rotation");
            f->letterSpacing = slot(lengthExpr(a.value("letter_spacing", json(0.0)), "letter_spacing"), cad::ValueKind::Length,
                                    "letter_spacing");
            const json ls = a.value("line_spacing", json(1.2));
            f->lineSpacing = slot(ls.is_number() ? num(ls.get<double>()) : ls.get<std::string>(), cad::ValueKind::Scalar,
                                  "line_spacing");
            f->font = a.value("font", std::string("DejaVu Sans"));
            f->bold = a.value("bold", false);
            f->italic = a.value("italic", false);
            f->mirror = a.value("mirror", false);
            const std::string d = lower(a.value("direction", std::string("engrave")));
            if(d != "engrave" && d != "emboss") fail("direction must be engrave or emboss");
            f->direction = cad::textDirectionFromString(d);
            double before = 0.0;
            for(const auto &kv : st->bodies) before += cad::volumeOf(kv.second->shape.shape());
            json res = commit(f, "Create Text (MCP)");
            double after = 0.0;
            for(const auto &kv : settle()->bodies) after += cad::volumeOf(kv.second->shape.shape());
            res["volume_change_mm3"] = r3(after - before);
            const gp_Pnt mid = frame.point(x, y);
            res["text_middle"] = pt(mid);
            res["position"] = {r3(x), r3(y)};
            res["face_frame"] = frame.planar ? "planar: x and y in the face's plane (on a top face, world X and Y)"
                                             : "curved: mm along the surface from the middle of the face";
            return res;
        });

    // Bodies and / or features for mirror and pattern.
    auto patternObjects = [bodyOf, featureOf](const cad::StatePtr &st, const json &a, cad::PatternFeature &p) {
        for(const json &b : a.value("bodies", json::array())) p.bodies.push_back(bodyOf(st, b)->id);
        for(const json &f : a.value("features", json::array())) {
            const cad::FeaturePtr fp = featureOf(f);
            if(fp->type() != cad::FeatureType::Hole && fp->type() != cad::FeatureType::Extrude)
                fail(fp->name + " cannot be repeated (holes and extrudes can)");
            p.features.push_back(fp->id);
        }
        if(p.bodies.empty() == p.features.empty()) fail("give bodies or features (not both)");
        p.join = a.value("join", true);
        for(const json &k : a.value("skip", json::array())) p.skip.push_back(k.get<int>());
    };
    auto patternAxis = [topo](const cad::StatePtr &st, const json &v) {
        cad::PatternAxis ax;
        if(v.is_string()) {
            ax.builtin = lower(v.get<std::string>());
            if(ax.builtin != "x" && ax.builtin != "y" && ax.builtin != "z") fail("axis must be x, y, z or {edge} / {face}");
        } else if(v.is_object() && v.contains("edge")) {
            ax.ref = topo(st, v["edge"], cad::TopoKind::Edge);
        } else if(v.is_object() && v.contains("face")) {
            ax.ref = topo(st, v["face"], cad::TopoKind::Face);
        } else {
            fail("axis must be \"x\", \"y\", \"z\", {\"edge\": {body, index}} or {\"face\": {body, index}}");
        }
        return ax;
    };
    const json objectsDoc = {
        {"bodies", arrayOf(str("body id or name"), "bodies to copy")},
        {"features", arrayOf({{"description", "feature id or name"}, {"anyOf", json::array({{{"type", "integer"}}, {{"type", "string"}}})}},
                             "holes or extrudes to repeat (their copies cut / join like the original)")},
        {"join", boolean("bodies: join copies that touch the original (default true)")},
        {"skip", arrayOf(integer("copy number, 1 = first copy"), "copies to leave out")}};
    json mirrorArgs = objectsDoc;
    mirrorArgs["plane"] = planeSpec();
    add("mirror",
        "Mirrors bodies, or holes / extrudes, across a plane (XY, XZ, YZ, a construction plane or a planar face). "
        "Symmetric parts: model half, mirror it with join.",
        mirrorArgs, {"plane"}, [begin, settle, planeOf, commit, patternObjects](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto p = std::make_shared<cad::PatternFeature>();
            p->kind = cad::PatternKind::Mirror;
            patternObjects(st, a, *p);
            p->plane = planeOf(st, a.at("plane"));
            return commit(p, "Mirror (MCP)");
        });
    json patternArgs = objectsDoc;
    patternArgs["type"] = enumOf({"rectangular", "circular"}, "rectangular (rows along directions) or circular (around an axis)");
    patternArgs["direction"] = {{"description", "rectangular: \"x\", \"y\", \"z\" or {\"edge\": {body, index}}"}};
    patternArgs["count"] = numberOrExpr("instances including the original (rectangular default 3, circular 6)");
    patternArgs["spacing"] = numberOrExpr("rectangular: mm between instances (negative: the other way)");
    patternArgs["direction2"] = {{"description", "rectangular: an optional second direction"}};
    patternArgs["count2"] = numberOrExpr("instances along direction2 (default 2)");
    patternArgs["spacing2"] = numberOrExpr("mm along direction2");
    patternArgs["axis"] = {{"description", "circular: \"x\", \"y\", \"z\" (through the origin), {\"edge\": ...} (a straight "
                                           "or round edge) or {\"face\": ...} (a cylinder: a hole's axis)"}};
    patternArgs["angle"] = numberOrExpr("circular: total degrees (default 360: evenly all round)");
    patternArgs["symmetric"] = boolean("circular: spread both ways from the original");
    add("pattern",
        "Repeats bodies, or holes / extrudes, in rows (rectangular: count and spacing along one or two directions) or "
        "around an axis (circular: count over a total angle) - bolt circles, rows of holes, grids of pegs.",
        patternArgs, {"type"}, [begin, settle, commit, slot, patternObjects, patternAxis](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto p = std::make_shared<cad::PatternFeature>();
            patternObjects(st, a, *p);
            auto scalar = [&](const json &v, const char *what) {
                const std::string e = v.is_number() ? num(v.get<double>()) : v.is_string() ? v.get<std::string>() : "";
                if(e.empty()) fail(std::string(what) + " must be a number or an expression");
                return slot(e, cad::ValueKind::Scalar, what);
            };
            if(a.at("type") == "circular") {
                p->kind = cad::PatternKind::Circular;
                p->axis = patternAxis(st, a.value("axis", json("z")));
                p->count = scalar(a.value("count", json(6)), "count");
                p->angle = slot(angleExpr(a.value("angle", json(360.0)), "angle"), cad::ValueKind::Angle, "angle");
                p->symmetric = a.value("symmetric", false);
            } else {
                p->kind = cad::PatternKind::Rectangular;
                p->dir1 = patternAxis(st, a.value("direction", json("x")));
                p->count1 = scalar(a.value("count", json(3)), "count");
                if(!a.contains("spacing")) fail("give the spacing");
                p->spacing1 = slot(lengthExpr(a["spacing"], "spacing"), cad::ValueKind::Length, "spacing");
                if(a.contains("direction2")) {
                    p->dir2 = patternAxis(st, a["direction2"]);
                    p->count2 = scalar(a.value("count2", json(2)), "count2");
                    if(!a.contains("spacing2")) fail("give spacing2");
                    p->spacing2 = slot(lengthExpr(a["spacing2"], "spacing2"), cad::ValueKind::Length, "spacing2");
                }
            }
            return commit(p, "Pattern (MCP)");
        });

    add("thread",
        "Threads round faces: a hole's wall (internal thread, to take a screw) or a boss (external, a bolt). The size "
        "comes from the diameter (a hole drilled at a tap drill or minor diameter, a boss at the major diameter) unless "
        "given. Small threads print poorly, so by default M4 / #8 and smaller are only opened to the tap drill (tap "
        "them after printing, or use self-tapping screws or heat-set inserts); mode \"modeled\" forces real threads. "
        "clearance (radial) makes printed threads fit. Sizes: ISO M2-M24 coarse, M8x1..M24x2 fine, UNC and UNF #4 to "
        "1\".",
        {{"faces", arrayOf(topoItem("face"), "round faces (list_faces with type cylinder)")},
         {"size", str("e.g. \"M6\", \"M8x1\", \"1/4-20 UNC\"; default: from the diameter")},
         {"mode", enumOf({"auto", "modeled", "tap_drill"}, "default auto")},
         {"clearance", numberOrExpr("radial print clearance, mm (default 0.15)")},
         {"length", numberOrExpr("thread length from the open end, mm (default the whole face)")},
         {"left_hand", boolean("left-handed thread")}},
        {"faces"}, [begin, settle, topo, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto t = std::make_shared<cad::ThreadFeature>();
            for(const json &f : a.at("faces")) t->faces.push_back(topo(st, f, cad::TopoKind::Face));
            if(t->faces.empty()) fail("give at least one face");
            if(a.contains("size")) {
                const cad::ThreadSpec *spec = cad::findThread(a["size"].get<std::string>());
                if(!spec) fail("unknown thread size " + a["size"].get<std::string>());
                t->size = spec->name;
            }
            const std::string m = a.value("mode", "auto");
            t->mode = m == "modeled" ? cad::ThreadMode::Modeled : m == "tap_drill" ? cad::ThreadMode::TapDrill : cad::ThreadMode::Auto;
            t->clearance = slot(lengthExpr(a.value("clearance", json(0.15)), "clearance"), cad::ValueKind::Length, "clearance");
            if(a.contains("length")) t->length = slot(lengthExpr(a["length"], "length"), cad::ValueKind::Length, "length");
            t->leftHand = a.value("left_hand", false);
            return commit(t, "Thread (MCP)");
        });

    add("draft",
        "Tilts flat faces about a hinge edge (a draft). The hinge is a straight edge of one of the faces (list_edges); "
        "the face on its other side sets the pull direction and stays put. A positive angle leans the faces in over "
        "the body - a taper that prints without support; lean_out tilts them out instead. Give all the walls of a "
        "box with one bottom edge to taper the whole box.",
        {{"faces", arrayOf(topoItem("face"), "flat faces to tilt")},
         {"hinge", topoItem("edge")},
         {"angle", numberOrExpr("degrees (default 5)")},
         {"lean_out", boolean("tilt out over the hinge instead of in (default false)")}},
        {"faces", "hinge"}, [begin, settle, topo, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto d = std::make_shared<cad::DraftFeature>();
            for(const json &f : a.at("faces")) d->faces.push_back(topo(st, f, cad::TopoKind::Face));
            if(d->faces.empty()) fail("give at least one face");
            d->hinge = topo(st, a.at("hinge"), cad::TopoKind::Edge);
            d->angle = slot(angleExpr(a.value("angle", json(5.0)), "angle"), cad::ValueKind::Angle, "angle");
            d->flip = a.value("lean_out", false);
            return commit(d, "Draft (MCP)");
        });

    add("revolve",
        "Turns sketch profiles (regions, picked by points inside them; default every profile of the sketch) about an "
        "axis into a solid of revolution - knobs, bottles, spacers, pulleys, rings. The axis is \"x\" / \"y\" / \"z\" "
        "(world), {\"sketch_line\": [sketch, line id]} (a line of a sketch, often a construction centre line; ids from "
        "the sketch's curves; or \"x_axis\" / \"y_axis\" for that sketch's own axes), or {\"edge\"} / {\"face\"} of a "
        "body. The profile must lie on one side of the axis (it may touch it). Angle 360 (default) makes a closed "
        "solid. Like extrude it makes a new body by default.",
        {{"sketch", integer("sketch feature id")},
         {"profile_points", arrayOf(xy("a point inside the region"), "which regions of the sketch (sketch coordinates)")},
         {"axis", {{"description", "\"x\" | \"y\" | \"z\" | {\"sketch_line\": [sketch, id | \"x_axis\" | \"y_axis\"]} | "
                                   "{\"edge\": {body, index}} | {\"face\": {body, index}}"}}},
         {"angle", numberOrExpr("degrees, default 360")},
         {"extent", enumOf({"one_side", "symmetric", "two_sides"}, "default one_side; symmetric turns the angle each way")},
         {"angle2", numberOrExpr("two_sides: degrees the other way")},
         {"reverse", boolean("turn the other way round")},
         {"operation", enumOf({"new_body", "join", "cut", "intersect"}, "default new_body")}},
        {"sketch", "axis"}, [begin, settle, patternAxis, commit, sketchResultOf, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto r = std::make_shared<cad::RevolveFeature>();
            const cad::FeatureId sid = a.at("sketch").get<int>();
            const cad::SketchResult *sk = sketchResultOf(st, sid);
            if(sk->profiles.empty()) fail("the sketch has no closed profiles");
            auto addProfile = [&](const cad::Profile &p) {
                cad::ProfileRef pr{sid, p.key, p.sample};
                cad::captureProfileOutline(*st, pr);
                r->profiles.push_back(pr);
            };
            if(a.contains("profile_points")) {
                for(const json &q : a["profile_points"]) {
                    const cad::Vec2 p = vec2Arg(q, "profile point");
                    const cad::Profile *hit = nullptr;
                    for(const auto &pr : sk->profiles)
                        if(pr.contains(p) && (!hit || std::fabs(pr.area) < std::fabs(hit->area))) hit = &pr;
                    if(!hit) fail("no profile of the sketch contains [" + num(p.x) + ", " + num(p.y) + "]");
                    addProfile(*hit);
                }
            } else {
                for(const auto &pr : sk->profiles) addProfile(pr);
            }
            const json &ax = a.at("axis");
            if(ax.is_object() && ax.contains("sketch_line")) {
                const json &sl = ax["sketch_line"];
                if(!sl.is_array() || sl.size() != 2) fail("sketch_line is [sketch id, line id]");
                r->axis.sketch = sl[0].get<int>();
                if(sl[1].is_string()) {
                    const std::string w = sl[1].get<std::string>();
                    r->axis.line = w == "x_axis" ? cad::kSketchXAxis : w == "y_axis" ? cad::kSketchYAxis : 0;
                    if(!r->axis.line) fail("sketch_line's line is an id, \"x_axis\" or \"y_axis\"");
                } else {
                    r->axis.line = sl[1].get<int>();
                }
                sketchResultOf(st, r->axis.sketch);
            } else {
                r->axis.axis = patternAxis(st, ax);
            }
            const std::string ext = a.value("extent", "one_side");
            r->extent = cad::revolveExtentFromString(ext == "two_sides" ? "twoSides" : ext);
            r->angle = slot(angleExpr(a.value("angle", json(360.0)), "angle"), cad::ValueKind::Angle, "angle");
            if(r->extent == cad::RevolveExtent::TwoSides)
                r->angle2 = slot(angleExpr(a.value("angle2", json(90.0)), "angle2"), cad::ValueKind::Angle, "angle2");
            r->flip = a.value("reverse", false);
            const std::string op = a.value("operation", "new_body");
            r->operation = op == "join" ? cad::BodyOperation::Join
                           : op == "cut" ? cad::BodyOperation::Cut
                           : op == "intersect" ? cad::BodyOperation::Intersect
                                               : cad::BodyOperation::NewBody;
            return commit(r, "Create Revolve (MCP)");
        });

    add("shell",
        "Hollows bodies out with walls of an even thickness - enclosures, boxes, cups. Give the faces to remove (the "
        "openings, e.g. a box's top; list_faces) or bodies to hollow with a sealed void inside. direction inside "
        "(default) keeps the outer size, outside keeps the inner size. Walls under 0.8 mm warn: they print poorly.",
        {{"faces", arrayOf(topoItem("face"), "faces to remove")},
         {"bodies", arrayOf(str("body id or name"), "bodies to hollow with no opening")},
         {"thickness", numberOrExpr("wall thickness in mm (default 2)")},
         {"direction", enumOf({"inside", "outside"}, "default inside")}},
        {}, [begin, settle, topo, bodyOf, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto sh = std::make_shared<cad::ShellFeature>();
            for(const json &f : a.value("faces", json::array())) sh->faces.push_back(topo(st, f, cad::TopoKind::Face));
            for(const json &b : a.value("bodies", json::array())) sh->bodies.push_back(bodyOf(st, b)->id);
            if(sh->faces.empty() && sh->bodies.empty()) fail("give the faces to remove, or bodies to hollow");
            sh->thickness = slot(lengthExpr(a.value("thickness", json(2.0)), "thickness"), cad::ValueKind::Length, "thickness");
            sh->direction = a.value("direction", "inside") == "outside" ? cad::ShellDirection::Outside
                                                                        : cad::ShellDirection::Inside;
            return commit(sh, "Create Shell (MCP)");
        });

    add("split_body",
        "Splits bodies in two (or more) with a plane - \"XY\"/\"XZ\"/\"YZ\", a construction plane or a planar face, "
        "all unbounded - or with the curves of a sketch swept both ways along its normal. Every piece becomes a body "
        "(the biggest keeps the name). For parts too big for the printer: keep \"both\" and set pins to drill matching "
        "alignment pin holes into both halves of a plane cut.",
        {{"plane", planeSpec()},
         {"sketch", integer("split with this sketch's curves instead of a plane")},
         {"curves", arrayOf(integer("sketch entity id"), "only these curves of the sketch (default all non-construction)")},
         {"bodies", arrayOf(str("body id or name"), "bodies to split (default every body the tool crosses)")},
         {"keep", enumOf({"both", "front", "back"}, "plane splits: keep both sides (default) or only the side the "
                                                     "plane normal points to (front) or the other")},
         {"pins", boolean("drill alignment pin holes into both halves (plane splits keeping both)")},
         {"pin_diameter", numberOrExpr("pin hole diameter, mm (default 3.2)")},
         {"pin_depth", numberOrExpr("pin hole depth into each half, mm (default 6)")}},
        {}, [begin, settle, planeOf, bodyOf, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto sp = std::make_shared<cad::SplitFeature>();
            if(a.contains("sketch")) {
                sp->tool = cad::SplitTool::Sketch;
                sp->sketch = a["sketch"].get<int>();
                if(!st->sketches.count(sp->sketch)) fail("no sketch " + std::to_string(sp->sketch) + " (see get_design)");
                sp->curves = a.value("curves", std::vector<int>{});
            } else if(a.contains("plane")) {
                sp->plane = planeOf(st, a["plane"]);
            } else {
                fail("give a plane or a sketch to split with");
            }
            for(const json &b : a.value("bodies", json::array())) sp->bodies.push_back(bodyOf(st, b)->id);
            const std::string keep = a.value("keep", "both");
            sp->keep = keep == "front" ? cad::SplitKeep::Front : keep == "back" ? cad::SplitKeep::Back : cad::SplitKeep::Both;
            sp->pins = a.value("pins", false);
            sp->pinDiameter = slot(lengthExpr(a.value("pin_diameter", json(3.2)), "pin_diameter"), cad::ValueKind::Length,
                                   "pin_diameter");
            sp->pinDepth = slot(lengthExpr(a.value("pin_depth", json(6.0)), "pin_depth"), cad::ValueKind::Length, "pin_depth");
            return commit(sp, "Split Body (MCP)");
        });

    add("combine",
        "Joins, cuts or intersects bodies: the target body with the tool bodies. keep_tools leaves the tools in place "
        "(for example to cut a clearance pocket and keep the part that fits in it).",
        {{"target", str("the body that is changed")},
         {"tools", arrayOf(str("body id or name"), "tool bodies")},
         {"operation", enumOf({"join", "cut", "intersect"}, "default join")},
         {"keep_tools", boolean("keep the tool bodies (default false)")}},
        {"target", "tools"}, [begin, settle, bodyOf, commit](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto c = std::make_shared<cad::CombineFeature>();
            c->target = bodyOf(st, a.at("target"))->id;
            for(const json &t : a.at("tools")) {
                const cad::BodyId id = bodyOf(st, t)->id;
                if(id == c->target) fail("a body cannot be combined with itself");
                c->tools.push_back(id);
            }
            if(c->tools.empty()) fail("give at least one tool body");
            const std::string op = a.value("operation", "join");
            c->operation = op == "cut" ? cad::BodyOperation::Cut : op == "intersect" ? cad::BodyOperation::Intersect : cad::BodyOperation::Join;
            c->keepTools = a.value("keep_tools", false);
            return commit(c, "Create Combine (MCP)");
        });

    add("offset_plane",
        "A construction plane offset from an origin plane, another construction plane or a planar face, optionally "
        "tilted about its own X axis (tilt_x), its Y axis (tilt_y) or both - X first, then the tilted Y - turning "
        "about the plane's centre. Sketch on it with create_sketch {plane: {plane: id}}.",
        {{"base", planeSpec()},
         {"offset", numberOrExpr("distance along the base normal, mm (default 0)")},
         {"tilt_x", numberOrExpr("tilt about the plane's X axis, degrees (default 0)")},
         {"tilt_y", numberOrExpr("tilt about the plane's Y axis (after tilt_x), degrees (default 0)")},
         {"angle", numberOrExpr("older form: a single tilt in degrees about `axis`")},
         {"axis", enumOf({"x", "y"}, "older form: the axis `angle` turns about (default x)")}},
        {"base"}, [begin, settle, planeOf, commit, slot](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto p = std::make_shared<cad::ConstructionPlaneFeature>();
            p->base = planeOf(st, a.at("base"));
            p->offset = slot(lengthExpr(a.value("offset", json(0.0)), "offset"), cad::ValueKind::Length, "offset");
            json tx = a.value("tilt_x", json()), ty = a.value("tilt_y", json());
            if(a.contains("angle")) {
                json &legacy = a.value("axis", "x") == "y" ? ty : tx;
                if(legacy.is_null()) legacy = a.at("angle");
            }
            p->angle = slot(angleExpr(tx.is_null() ? json(0.0) : tx, "tilt_x"), cad::ValueKind::Angle, "tilt_x");
            p->angleY = slot(angleExpr(ty.is_null() ? json(0.0) : ty, "tilt_y"), cad::ValueKind::Angle, "tilt_y");
            p->axis = cad::PlaneRotationAxis::LocalX;
            return commit(p, "Create Plane (MCP)");
        });

    // --- timeline ---------------------------------------------------------------------------------
    add("edit_feature",
        "Changes values of an existing feature (one undo step), like Edit Feature: e.g. {\"distance\": 25} for an "
        "extrude, {\"radius\": \"3 mm\"} for a fillet, {\"operation\": \"cut\"}. Keys are those get_design shows in the "
        "feature data (see set_parameter for any parameter by name). Numbers are mm, or degrees for angles.",
        {{"feature", {{"description", "feature id or name"}, {"anyOf", json::array({{{"type", "integer"}}, {{"type", "string"}}})}}},
         {"set", {{"type", "object"}, {"description", "key -> new value"}}},
         {"name", str("rename it")}},
        {"feature"}, [&doc, begin, settle, featureOf, bodiesJson, checkExpr](const json &a) {
            begin();
            const cad::FeaturePtr f = featureOf(a.at("feature"));
            auto copy = f->clone();
            json data = copy->dataToJson();
            static const std::map<std::string, std::string> enumKeys = {{"new_body", "newBody"}, {"one_side", "oneSide"},
                                                                        {"two_sides", "twoSides"}};
            const json changes = a.value("set", json::object());
            for(auto &[key, value] : changes.items()) {
                if(!data.contains(key)) {
                    std::string keys;
                    for(auto &[k, v] : data.items()) keys += (keys.empty() ? "" : ", ") + k;
                    fail("feature " + f->name + " has no \"" + key + "\" (it has: " + keys + ")");
                }
                json &slotJ = data[key];
                if(slotJ.is_object() && slotJ.contains("expr")) {
                    const bool angle = key.find("angle") != std::string::npos || key.find("taper") != std::string::npos;
                    const std::string expr = angle ? angleExpr(value, key.c_str()) : lengthExpr(value, key.c_str());
                    checkExpr(expr, angle ? cad::ValueKind::Angle : cad::ValueKind::Length, key.c_str());
                    slotJ["expr"] = expr;
                } else if(value.is_string() && enumKeys.count(value.get<std::string>())) {
                    slotJ = enumKeys.at(value.get<std::string>());
                } else {
                    slotJ = value;
                }
            }
            copy->dataFromJson(data);
            copy->id = f->id;
            copy->name = a.contains("name") ? a["name"].get<std::string>() : f->name;
            copy->suppressed = f->suppressed;
            doc.replaceFeature(copy, "Edit " + f->name + " (MCP)");
            const cad::StatePtr st = settle();
            const cad::Status s = doc.statusOf(f->id);
            if(s.isError()) {
                doc.undo();
                settle();
                fail(s.message + " (the change was undone)");
            }
            return json{{"feature", f->id}, {"status", severity(s.severity)}, {"message", s.message}, {"data", data},
                        {"bodies", bodiesJson(st)}};
        });

    add("set_parameter",
        "Sets a model parameter (a dimension, distance, radius...: names like \"d3\" from get_design) to a value or "
        "expression. Everything that depends on it recomputes, as in Fusion's Change Parameters.",
        {{"name", str("parameter name, e.g. d3")}, {"expression", numberOrExpr("new value (number = mm) or expression")}},
        {"name", "expression"}, [&doc, begin, settle, bodiesJson](const json &a) {
            begin();
            const std::string name = a.at("name");
            const cad::ParamValue *pv = doc.params().find(name);
            if(!pv) fail("no parameter \"" + name + "\" (see get_design)");
            const bool angle = pv->def.kind == cad::ValueKind::Angle;
            const json &v = a.at("expression");
            const std::string expr = v.is_number() ? num(v.get<double>()) + (angle ? " deg" : " mm") : v.get<std::string>();
            const cad::EvalResult check = doc.params().evaluateExpression(expr, pv->def.kind);
            if(!check.ok) fail("\"" + expr + "\": " + check.error);
            const cad::FeaturePtr owner = doc.feature(pv->def.owner);
            if(!owner) fail("parameter \"" + name + "\" has no feature");
            auto copy = owner->clone();
            json data = copy->dataToJson();
            bool found = false;
            std::function<void(json &)> walk = [&](json &j) {
                if(j.is_object()) {
                    if(j.contains("expr") && ((j.value("name", "") == name) || (j.value("param", "") == name))) {
                        j["expr"] = expr;
                        found = true;
                    }
                    for(auto &[k, sub] : j.items()) walk(sub);
                } else if(j.is_array()) {
                    for(auto &sub : j) walk(sub);
                }
            };
            walk(data);
            if(!found) fail("parameter \"" + name + "\" was not found in " + owner->name);
            copy->dataFromJson(data);
            doc.replaceFeature(copy, "Change " + name + " (MCP)");
            const cad::StatePtr st = settle();
            json errors = json::array();
            for(const auto &f : doc.features())
                if(doc.statusOf(f->id).isError()) errors.push_back({{"feature", f->name}, {"message", doc.statusOf(f->id).message}});
            if(!errors.empty()) {
                doc.undo();
                settle();
                fail("that value breaks the model, so it was undone: " + errors.dump());
            }
            return json{{"parameter", name}, {"expr", expr}, {"bodies", bodiesJson(st)}};
        });

    add("suppress", "Suppresses (or unsuppresses) a feature: it is skipped, as if it were not there.",
        {{"feature", {{"anyOf", json::array({{{"type", "integer"}}, {{"type", "string"}}})}}}, {"suppressed", boolean("default true")}},
        {"feature"}, [&doc, begin, settle, featureOf, bodiesJson](const json &a) {
            begin();
            const cad::FeaturePtr f = featureOf(a.at("feature"));
            doc.setSuppressed(f->id, a.value("suppressed", true));
            return json{{"feature", f->id}, {"suppressed", a.value("suppressed", true)}, {"bodies", bodiesJson(settle())}};
        });

    add("set_visibility",
        "Shows or hides bodies, sketches and construction planes (like the eyes in the browser), or whole browser folders: "
        "a hidden folder hides everything in it but keeps each item's own setting for when it is shown again. Items are "
        "ids or names. Returns what is visible afterwards.",
        {{"bodies", arrayOf(str("body id or name"), "bodies to show or hide")},
         {"sketches", arrayOf({{"anyOf", json::array({{{"type", "integer"}}, {{"type", "string"}}})}}, "sketch ids or names")},
         {"planes", arrayOf({{"anyOf", json::array({{{"type", "integer"}}, {{"type", "string"}}})}},
                            "construction plane ids or names")},
         {"folders", arrayOf(enumOf({"bodies", "sketches", "construction", "origin"}, "a browser folder"),
                             "folders to show or hide as a whole")},
         {"visible", boolean("true to show, false to hide")}},
        {"visible"}, [&doc, begin, settle, bodyOf, visibilityJson](const json &a) {
            begin();
            const bool on = a.at("visible").get<bool>();
            const cad::StatePtr st = settle();
            auto idOf = [](const auto &items, const json &v, const char *what) -> cad::FeatureId {
                for(const auto &[id, item] : items)
                    if((v.is_number_integer() && v.get<int>() == id) || (v.is_string() && v.get<std::string>() == item->name))
                        return id;
                fail(std::string("no ") + what + " " + v.dump() + " (see get_design)");
            };
            // Look everything up first, so a bad name changes nothing.
            std::vector<cad::BodyId> bodies;
            std::vector<cad::FeatureId> sketches, planes;
            for(const json &v : a.value("bodies", json::array())) bodies.push_back(bodyOf(st, v)->id);
            for(const json &v : a.value("sketches", json::array())) sketches.push_back(idOf(st->sketches, v, "sketch"));
            for(const json &v : a.value("planes", json::array())) planes.push_back(idOf(st->planes, v, "construction plane"));
            for(const json &v : a.value("folders", json::array())) {
                const auto &names = cad::Document::folderNames();
                if(!v.is_string() || std::find(names.begin(), names.end(), v.get<std::string>()) == names.end())
                    fail("folder must be bodies, sketches, construction or origin");
            }
            if(bodies.empty() && sketches.empty() && planes.empty() && a.value("folders", json::array()).empty())
                fail("name at least one body, sketch, plane or folder");
            for(const auto &id : bodies) doc.setBodyVisible(id, on);
            for(auto id : sketches) doc.setSketchVisible(id, on);
            for(auto id : planes) doc.setPlaneVisible(id, on);
            for(const json &v : a.value("folders", json::array())) doc.setFolderVisible(v.get<std::string>(), on);
            return visibilityJson(settle());
        });

    add("set_material",
        "Sets what bodies are printed in: the filament (PLA, PETG or TPU), its finish (matte, silk or semitransparent) "
        "and colour (a named filament colour such as \"Signal Red\", \"Silk Gold\", \"Ice Blue\", or \"#rrggbb\"). One "
        "undo step. The Rendered style and render_image show it (layer lines, silk sheen, light through "
        "semitransparent parts); default: grey matte PLA. Returns the bodies with their materials.",
        {{"bodies", arrayOf(str("body id or name"), "the bodies (all of them if left out)")},
         {"material", enumOf({"PLA", "PETG", "TPU"}, "filament (default: keep, or PLA)")},
         {"finish", enumOf({"matte", "silk", "semitransparent"}, "finish (default: keep, or matte)")},
         {"color", str("a named filament colour or #rrggbb (default: keep, or one that suits the finish)")},
         {"reset", boolean("back to the default grey matte PLA (ignores the rest)")}},
        {}, [&doc, begin, settle, bodyOf, bodiesJson](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            std::vector<cad::BodyId> ids;
            for(const json &v : a.value("bodies", json::array())) ids.push_back(bodyOf(st, v)->id);
            if(!a.contains("bodies"))
                for(const cad::Body *b : st->orderedBodies()) ids.push_back(b->id);
            if(ids.empty()) fail("there are no bodies");
            std::vector<cad::BodyId> all;
            for(const cad::Body *b : st->orderedBodies()) all.push_back(b->id);
            if(a.value("reset", false)) {
                doc.setBodyMaterials(ids, std::nullopt, all);
                return json{{"bodies", bodiesJson(settle())}};
            }
            // Each body keeps what is not given.
            std::optional<cad::PrintMaterial> pm;
            std::optional<cad::Finish> fin;
            if(a.contains("material")) {
                cad::PrintMaterial v;
                if(!cad::printMaterialFromString(a["material"].get<std::string>(), v)) fail("material must be PLA, PETG or TPU");
                pm = v;
            }
            if(a.contains("finish")) {
                cad::Finish v;
                if(!cad::finishFromString(a["finish"].get<std::string>(), v)) fail("finish must be matte, silk or semitransparent");
                fin = v;
            }
            std::optional<std::pair<uint32_t, std::string>> color;
            if(a.contains("color")) {
                uint32_t rgb;
                std::string name;
                if(!cad::parseColor(a["color"].get<std::string>(), rgb, name))
                    fail("unknown colour " + a["color"].dump() + " (a filament colour name or #rrggbb)");
                color = {{rgb, name}};
            }
            if(!pm && !fin && !color) fail("give a material, finish or color (or reset)");
            std::vector<std::pair<cad::BodyId, std::optional<cad::BodyMaterial>>> changes;
            for(const cad::BodyId &id : ids) changes.emplace_back(id, cad::withChanges(doc.bodyMaterial(id), pm, fin, color));
            doc.setBodyMaterials(changes, all);
            return json{{"bodies", bodiesJson(settle())}};
        });

    add("delete_feature", "Deletes a feature from the timeline (one undo step).",
        {{"feature", {{"anyOf", json::array({{{"type", "integer"}}, {{"type", "string"}}})}}}}, {"feature"},
        [&doc, begin, settle, featureOf, bodiesJson](const json &a) {
            begin();
            const cad::FeaturePtr f = featureOf(a.at("feature"));
            if(!doc.deleteFeature(f->id)) fail("could not delete " + f->name);
            return json{{"deleted", f->name}, {"bodies", bodiesJson(settle())}};
        });

    add("roll_to",
        "Moves the timeline's history marker: to a position (0 = before everything) or to just after a feature. New "
        "features are inserted at the marker.",
        {{"position", integer("number of active features")},
         {"after", {{"description", "feature id or name"}, {"anyOf", json::array({{{"type", "integer"}}, {{"type", "string"}}})}}}},
        {}, [&doc, begin, settle, featureOf, bodiesJson](const json &a) {
            begin();
            int pos = int(doc.features().size());
            if(a.contains("after")) pos = doc.indexOf(featureOf(a["after"])->id) + 1;
            else if(a.contains("position")) pos = std::clamp(a["position"].get<int>(), 0, int(doc.features().size()));
            doc.setMarker(pos);
            return json{{"marker", doc.marker()}, {"bodies", bodiesJson(settle())}};
        });

    add("undo", "Undoes the last change (like Cmd+Z). Optional count.", {{"count", integer("how many steps, default 1")}}, {},
        [&doc, begin, settle, bodiesJson](const json &a) {
            begin();
            int n = 0;
            json labels = json::array();
            for(int i = 0; i < std::max(1, a.value("count", 1)); ++i) {
                const std::string l = doc.undoLabel();
                if(!doc.undo()) break;
                labels.push_back(l);
                ++n;
            }
            if(!n) fail("nothing to undo");
            return json{{"undone", labels}, {"bodies", bodiesJson(settle())}};
        });

    add("redo", "Redoes what was undone. Optional count.", {{"count", integer("how many steps, default 1")}}, {},
        [&doc, begin, settle, bodiesJson](const json &a) {
            begin();
            int n = 0;
            json labels = json::array();
            for(int i = 0; i < std::max(1, a.value("count", 1)); ++i) {
                const std::string l = doc.redoLabel();
                if(!doc.redo()) break;
                labels.push_back(l);
                ++n;
            }
            if(!n) fail("nothing to redo");
            return json{{"redone", labels}, {"bodies", bodiesJson(settle())}};
        });

    // --- view -------------------------------------------------------------------------------------
    add("set_view", "Points the camera: home, front, back, left, right, top, bottom (and fits the model).",
        {{"view", enumOf({"home", "front", "back", "left", "right", "top", "bottom"}, "")}}, {"view"}, [this](const json &a) {
            const std::map<std::string, StandardView> views = {{"home", StandardView::Home}, {"front", StandardView::Front},
                                                               {"back", StandardView::Back}, {"left", StandardView::Left},
                                                               {"right", StandardView::Right}, {"top", StandardView::Top},
                                                               {"bottom", StandardView::Bottom}};
            const std::string v = a.at("view");
            if(!views.count(v)) fail("unknown view " + v);
            m_w.viewport()->setStandardView(views.at(v));
            m_w.viewport()->fitAll();
            return json{{"view", v}};
        });

    add("set_render",
        "How the design is rendered (the Rendered style and render_image): the build plate (the Snapmaker U1's textured "
        "or smooth PEI sheet, or none), where the model sits on it (centered, or as_modelled with the print area's "
        "front-left corner at the origin, as in a slicer), studio or daylight lighting, the print's layer height, "
        "line width and infill and whether layer lines show, whether the canvas refines with the path tracer when the view "
        "rests, and draft or final quality. Not an undo step. Switches the canvas to the Rendered style (unless "
        "show is false). Returns the settings.",
        {{"plate", enumOf({"textured_pei", "smooth_pei", "none"}, "build plate sheet")},
         {"placement", enumOf({"centered", "as_modelled"}, "where the model sits on the plate")},
         {"lighting", enumOf({"studio", "daylight"}, "studio softboxes, or sun and sky")},
         {"layer_height", number("mm (0.04-0.6; default 0.2)")},
         {"line_width", number("mm (0.1-2; default 0.42)")},
         {"layer_lines", boolean("show the layer lines")},
         {"infill_percent", number("infill of the prints, 0-100 (default 15): how much light gets through semitransparent parts")},
         {"ray_traced", boolean("refine the canvas with the path tracer when the view rests")},
         {"quality", enumOf({"draft", "final"}, "draft (fast) or final (every pixel, more samples)")},
         {"show", boolean("switch the canvas to the Rendered style (default true)")}},
        {}, [this, &doc](const json &a) {
            json j = doc.renderSettings().toJson();
            const std::map<std::string, std::string> keys = {{"plate", "plate"},           {"placement", "placement"},
                                                             {"lighting", "lighting"},     {"layer_height", "layerHeight"},
                                                             {"line_width", "lineWidth"},  {"layer_lines", "layerLines"},
                                                             {"ray_traced", "rayTraced"},  {"quality", "quality"}};
            for(const auto &[from, to] : keys)
                if(a.contains(from)) j[to] = a[from];
            if(a.contains("infill_percent")) {
                if(!a["infill_percent"].is_number()) fail("infill_percent must be a number (0-100)");
                j["infill"] = a["infill_percent"].get<double>() / 100.0;
            }
            const std::map<std::string, std::vector<std::string>> allowed = {
                {"plate", {"textured_pei", "smooth_pei", "none"}}, {"placement", {"centered", "as_modelled"}},
                {"lighting", {"studio", "daylight"}},              {"quality", {"draft", "final"}}};
            for(const auto &[key, values] : allowed)
                if(a.contains(key) && (!a[key].is_string() ||
                                       std::find(values.begin(), values.end(), a[key].get<std::string>()) == values.end()))
                    fail(key + " must be one of the listed values");
            for(const char *key : {"layer_height", "line_width"})
                if(a.contains(key) && !a[key].is_number()) fail(std::string(key) + " must be a number (mm)");
            doc.setRenderSettings(cad::RenderSettings::fromJson(j));
            if(a.value("show", true)) m_w.viewport()->setDisplayStyle(DisplayStyle::Rendered);
            return renderJson(doc.renderSettings());
        });

    add("render_image",
        "A lifelike, path-traced picture of the current view, as the Rendered style shows it (each body's material, "
        "the build plate, the lighting; see set_material and set_render), saved as a PNG. Takes a few seconds to "
        "a minute (samples per pixel; denoised). Returns the file's path.",
        {{"path", str("where to write the PNG (default: a file in the temporary folder)")},
         {"width", integer("pixels (default 1600)")},
         {"height", integer("pixels (default 1000)")},
         {"samples", integer("samples per pixel, 4-1024 (default 64)")}},
        {}, [this, begin, settle](const json &a) {
            begin();
            settle();
            const int w = a.value("width", 1600), h = a.value("height", 1000);
            if(w < 16 || h < 16 || w > 8192 || h > 8192) fail("width and height must be 16-8192");
            const int samples = std::clamp(a.value("samples", 64), 4, 1024);
            std::string path = a.value("path", std::string());
            if(path.empty())
                path = QDir(QDir::tempPath()).filePath(QStringLiteral("cadjitsu-render-%1.png").arg(QDateTime::currentMSecsSinceEpoch())).toStdString();
            Viewport *vp = m_w.viewport();
            RenderScene scene = vp->content();
            const TracedPicture pic = renderPicture(scene, vp->camera(), QSize(w, h), samples, vp->backgroundTop(),
                                                    vp->backgroundBottom());
            if(!pic.image.save(QString::fromStdString(path), "PNG")) fail("could not write " + path);
            return json{{"path", path}, {"width", w}, {"height", h}, {"samples", pic.samples}, {"denoised", pic.denoised}};
        });

    add("set_display_style", "Display style of the canvas: shaded_edges, shaded, wireframe or rendered.",
        {{"style", enumOf({"shaded_edges", "shaded", "wireframe", "rendered"}, "")}}, {"style"}, [this](const json &a) {
            const std::map<std::string, DisplayStyle> styles = {{"shaded_edges", DisplayStyle::ShadedWithEdges},
                                                                {"shaded", DisplayStyle::Shaded},
                                                                {"wireframe", DisplayStyle::Wireframe},
                                                                {"rendered", DisplayStyle::Rendered}};
            const std::string s = a.at("style");
            if(!styles.count(s)) fail("unknown style " + s);
            m_w.viewport()->setDisplayStyle(styles.at(s));
            return json{{"style", s}};
        });

    add("measure",
        "Measures between two things, like Inspect > Measure: the minimum distance and its closest points, the X/Y/Z "
        "components, the distance between centres (holes, circles, cylinder axes) and the angle between flat faces "
        "or straight edges. With only `a`, gives its own size (area, length, radius, volume). Changes nothing.",
        {{"a", measureItem("the first thing")}, {"b", measureItem("the second thing (optional)")}},
        {"a"}, [bodyOf, settle, begin](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            auto shapeOf = [&](const json &v) -> TopoDS_Shape {
                if(!v.is_object()) fail("a and b must be objects");
                if(v.contains("point")) {
                    const json &p = v["point"];
                    if(!p.is_array() || p.size() != 3) fail("point must be [x, y, z]");
                    return BRepBuilderAPI_MakeVertex(gp_Pnt(p[0].get<double>(), p[1].get<double>(), p[2].get<double>())).Shape();
                }
                const cad::Body *b = bodyOf(st, v.value("body", json()));
                for(const auto &[key, kind] : {std::pair{"face", cad::TopoKind::Face}, std::pair{"edge", cad::TopoKind::Edge},
                                               std::pair{"vertex", cad::TopoKind::Vertex}})
                    if(v.contains(key)) {
                        const int i = v[key].get<int>();
                        if(i < 1 || i > b->shape.count(kind))
                            fail(std::string(key) + " index " + std::to_string(i) + " is not in 1.." +
                                 std::to_string(b->shape.count(kind)) + " for " + b->id);
                        return b->shape.shapeOf(kind, i);
                    }
                return b->shape.shape();
            };
            const cad::KernelLock lock(cad::kernelMutex());
            const TopoDS_Shape s1 = shapeOf(a.at("a"));
            json out;
            if(!a.contains("b") || a["b"].is_null()) {
                std::vector<cad::Measurement> m;
                switch(s1.ShapeType()) {
                case TopAbs_FACE: m = cad::measureFace(TopoDS::Face(s1)); break;
                case TopAbs_EDGE: m = cad::measureEdge(TopoDS::Edge(s1)); break;
                case TopAbs_VERTEX: m = cad::measureVertex(TopoDS::Vertex(s1)); break;
                default: m = cad::measureBody(s1); break;
                }
                json props = json::object();
                for(const cad::Measurement &x : m) {
                    if(x.unit == cad::MeasureUnit::Text) props[x.label.empty() ? "type" : x.label] = x.text;
                    else props[x.label] = r3(x.unit == cad::MeasureUnit::Angle ? x.value * 180.0 / M_PI : x.value);
                }
                out["properties"] = props;
                out["units"] = "mm, mm2, mm3, degrees";
                return out;
            }
            const cad::MeasureResult r = cad::measureBetween(s1, shapeOf(a["b"]));
            if(!r.ok) fail(r.error);
            const gp_Vec d(r.p1, r.p2);
            out["distance"] = r3(r.distance);
            out["closest_points"] = json::array({pt(r.p1), pt(r.p2)});
            out["delta"] = json::array({r3(d.X()), r3(d.Y()), r3(d.Z())});
            if(r.centreDistance) {
                out["centre_distance"] = r3(*r.centreDistance);
                out["centres"] = json::array({pt(r.c1), pt(r.c2)});
            }
            if(r.angle) out["angle"] = r3(*r.angle * 180.0 / M_PI);
            out["units"] = "mm, degrees";
            return out;
        });

    add("overhangs",
        "Overhang check for printing upwards (+Z): for each body, the area of downward faces leaning further from "
        "vertical than `threshold` (they need support), flat bridges, and faces near the limit, with the worst faces "
        "(index for list_faces). The build plate is the lowest body's bottom. Fix with chamfers (45 deg) or by "
        "reorienting.",
        {{"threshold", {{"type", "number"}, {"description", "degrees from vertical a face may lean (default 45)"}}}},
        {}, [&doc, begin, settle](const json &a) {
            begin();
            const cad::StatePtr st = settle();
            cad::OverhangOptions o;
            o.threshold = a.value("threshold", 45.0);
            if(o.threshold <= 0 || o.threshold >= 90) fail("threshold must be between 0 and 90 degrees");
            std::vector<std::pair<const cad::Body *, std::shared_ptr<const cad::MeshData>>> meshes;
            double lowest = 1e300;
            for(const cad::Body *b : st->orderedBodies()) {
                meshes.push_back({b, b->mesh()});
                if(meshes.back().second) lowest = std::min(lowest, double(meshes.back().second->bboxMin[2]));
            }
            o.plateZ = lowest < 1e299 ? lowest : 0.0;
            json bodies = json::array();
            double total = 0;
            for(const auto &[b, m] : meshes) {
                if(!m) continue;
                const cad::OverhangReport r = cad::analyzeOverhangs(*m, o, false);
                std::vector<std::pair<double, int>> worst;
                for(const auto &[face, area] : r.faceOverhangArea) worst.push_back({area, face});
                std::sort(worst.rbegin(), worst.rend());
                json faces = json::array();
                for(size_t i = 0; i < worst.size() && i < 8; ++i)
                    faces.push_back({{"index", worst[i].second}, {"area", r3(worst[i].first)}});
                total += r.area[size_t(cad::OverhangKind::Overhang)] + r.area[size_t(cad::OverhangKind::Bridge)];
                bodies.push_back({{"body", doc.bodyName(*b)},
                                  {"needs_support_area", r3(r.area[size_t(cad::OverhangKind::Overhang)])},
                                  {"bridge_area", r3(r.area[size_t(cad::OverhangKind::Bridge)])},
                                  {"near_limit_area", r3(r.area[size_t(cad::OverhangKind::Near)])},
                                  {"on_plate_area", r3(r.area[size_t(cad::OverhangKind::Plate)])},
                                  {"worst_faces", faces}});
            }
            return json{{"threshold", o.threshold}, {"build_plate_z", r3(o.plateZ)}, {"bodies", bodies},
                        {"prints_without_support", total < 0.05}, {"units", "mm2"}};
        });

    add("section",
        "Section analysis: cuts the view (not the model) by a plane moved along its normal, to look inside. hide: true "
        "turns sections off.",
        {{"plane", planeSpec()}, {"offset", {{"type", "number"}, {"description", "mm along the plane normal"}}},
         {"flip", boolean("keep the other side")}, {"hide", boolean("turn all sections off")}},
        {}, [&doc, begin, settle, planeOf](const json &a) {
            begin();
            if(a.value("hide", false)) {
                for(const auto &s : doc.sections()) doc.setSectionVisible(s.id, false);
                return json{{"sections", "hidden"}};
            }
            const cad::StatePtr st = settle();
            cad::SectionAnalysis s;
            s.plane = planeOf(st, a.value("plane", json("XZ")));
            s.offset = a.value("offset", 0.0);
            s.flip = a.value("flip", false);
            const int id = doc.addSection(s);
            return json{{"section", id}, {"offset", s.offset}};
        });

    // --- files ------------------------------------------------------------------------------------
    auto path = [](const json &v) {
        if(!v.is_string() || v.get<std::string>().empty()) fail("give an absolute file path");
        QString p = QString::fromStdString(v.get<std::string>());
        if(p.startsWith(QLatin1String("~/"))) p = QDir::homePath() + p.mid(1);
        if(QFileInfo(p).isRelative()) fail("give an absolute file path");
        if(!QFileInfo(QFileInfo(p).absolutePath()).isDir()) fail("the folder " + QFileInfo(p).absolutePath().toStdString() + " does not exist");
        return p;
    };
    add("new_design", "Starts a new, empty design (the current one is discarded; save it first).", nullptr, {},
        [this, settle](const json &) {
            m_w.newDocument();
            settle();
            return json{{"design", "new"}};
        });
    add("open_design", "Opens a .cadjitsu design (or an older .cadly one).", {{"path", str("absolute path of a .cadjitsu or .cadly file")}}, {"path"},
        [this, &doc, settle, path, bodiesJson](const json &a) {
            const QString p = path(a.at("path"));
            cad::Document probe;
            std::string error;
            if(!probe.load(p.toStdString(), error)) fail("could not open: " + error);
            m_w.openFile(p);
            return json{{"opened", p.toStdString()}, {"features", doc.features().size()}, {"bodies", bodiesJson(settle())}};
        });
    add("save_design", "Saves the design as a .cadjitsu file (the whole history).", {{"path", str("absolute path, ending .cadjitsu")}},
        {"path"}, [this, path](const json &a) {
            QString p = path(a.at("path"));
            if(!p.endsWith(QLatin1String(".cadjitsu")) && !p.endsWith(QLatin1String(".cadly"))) p += QStringLiteral(".cadjitsu");
            if(!m_w.saveFile(p)) fail("could not save " + p.toStdString());
            return json{{"saved", p.toStdString()}};
        });
    auto exportJob = [this, settle, bodyOf](const json &a, ExportJob::Format format) {
        const cad::StatePtr st = settle();
        ExportJob job = makeExportJob(*m_w.modelView(), m_w.document(), format, false);
        if(a.contains("bodies")) {
            std::set<cad::BodyId> keep;
            for(const json &b : a["bodies"]) keep.insert(bodyOf(st, b)->id);
            job.solids.clear();
            for(const cad::Body *b : st->orderedBodies())
                if(keep.count(b->id)) {
                    cad::NamedSolid s;
                    s.name = m_w.document().bodyName(*b);
                    s.shape = b->shape.shape();
                    job.solids.push_back(std::move(s));
                }
        }
        if(job.solids.empty()) fail("there are no (visible) bodies to export");
        return job;
    };
    add("export_stl",
        "Exports bodies as an STL for 3D printing. The mesh is checked first (watertight, manifold, outward, volume); "
        "a mesh that fails is not written unless write_invalid. Returns the printability report.",
        {{"path", str("absolute path, ending .stl")},
         {"bodies", arrayOf(str("body id or name"), "default: every visible body")},
         {"refinement", enumOf({"coarse", "medium", "fine"}, "default medium")},
         {"merge", boolean("unite touching bodies into one solid (default true)")},
         {"binary", boolean("default true")},
         {"write_invalid", boolean("write even if the check fails")}},
        {"path"}, [begin, path, exportJob](const json &a) {
            begin();
            QString p = path(a.at("path"));
            if(!p.endsWith(QLatin1String(".stl"), Qt::CaseInsensitive)) p += QStringLiteral(".stl");
            ExportJob job = exportJob(a, ExportJob::Format::Stl);
            const std::string ref = a.value("refinement", "medium");
            job.stl.resolution = ref == "coarse" ? cad::StlResolution::Coarse : ref == "fine" ? cad::StlResolution::Fine : cad::StlResolution::Medium;
            job.stl.mergeBodies = a.value("merge", true);
            job.stl.binary = a.value("binary", true);
            const ExportResult r = runExport(job, p.toStdString(), a.value("write_invalid", false));
            json report = {{"watertight", r.report.watertight},     {"printable", r.report.ok},
                           {"triangles", r.report.triangles},        {"shells", r.report.shells},
                           {"mesh_volume_mm3", r3(r.report.volume)}, {"model_volume_mm3", r3(r.solidVolume)},
                           {"problems", r.report.problems}};
            if(!r.ok) return textResult(json{{"written", false}, {"error", r.error}, {"report", report}}, true);
            return textResult(json{{"written", p.toStdString()}, {"report", report}});
        });
    add("export_step", "Exports bodies as a STEP file (AP242 by default); it is read back to check the volume.",
        {{"path", str("absolute path, ending .step")},
         {"bodies", arrayOf(str("body id or name"), "default: every visible body")},
         {"schema", enumOf({"ap242", "ap214"}, "default ap242")}},
        {"path"}, [begin, path, exportJob](const json &a) {
            begin();
            QString p = path(a.at("path"));
            if(!p.endsWith(QLatin1String(".step"), Qt::CaseInsensitive) && !p.endsWith(QLatin1String(".stp"), Qt::CaseInsensitive))
                p += QStringLiteral(".step");
            ExportJob job = exportJob(a, ExportJob::Format::Step);
            job.schema = a.value("schema", "ap242") == "ap214" ? cad::StepSchema::AP214 : cad::StepSchema::AP242;
            const ExportResult r = runExport(job, p.toStdString());
            if(!r.ok) fail("STEP export failed: " + r.error);
            return json{{"written", p.toStdString()},
                        {"model_volume_mm3", r3(r.solidVolume)},
                        {"read_back_volume_mm3", r.reimported ? json(r3(r.reimportedVolume)) : json()}};
        });

    // --- batch -------------------------------------------------------------------------------
    add("batch",
        "Runs several tool calls in order, in one request: e.g. create_sketch, extrude, list_edges, fillet, get_design. "
        "Use it to build a part in a few turns instead of many. Each call is still its own undo step. Results come back "
        "per call, in order; by default the batch stops at the first failure (later calls are skipped) and reports it. "
        "Calls cannot use each other's results, so batch steps whose arguments you already know (sketch ids are "
        "predictable from get_design's features), then inspect and continue.",
        {{"calls", arrayOf({{"type", "object"},
                            {"properties", {{"tool", str("tool name, e.g. \"extrude\"")},
                                            {"arguments", {{"type", "object"}, {"description", "the tool's arguments"}}}}},
                            {"required", json::array({"tool"})}},
                           "the calls, run in order")},
         {"stop_on_error", boolean("stop at the first failed call (default true)")}},
        {"calls"}, [this](const json &a) {
            const json calls = a.at("calls");
            if(!calls.is_array() || calls.empty()) fail("calls must be a non-empty array of {tool, arguments}");
            const bool stop = a.value("stop_on_error", true);
            McpLog *log = m_w.mcpLog();
            json content = json::array();
            int failed = -1;
            for(size_t i = 0; i < calls.size(); ++i) {
                const json &c = calls[i];
                const std::string name = c.is_object() ? c.value("tool", "") : "";
                const json args = c.is_object() ? c.value("arguments", json::object()) : json::object();
                const std::string head = "[" + std::to_string(i + 1) + "/" + std::to_string(calls.size()) + "] " + name;
                if(failed >= 0 && stop) {
                    content.push_back({{"type", "text"}, {"text", head + ": skipped (an earlier call failed)"}});
                    continue;
                }
                json r;
                if(name == "batch") r = textResult("a batch cannot contain another batch", true);
                else if(name.empty()) r = textResult("each call needs a \"tool\"", true);
                else {
                    if(log) log->add(McpEvent::Kind::Call, QStringLiteral("batch"), QString::fromStdString(name),
                                     QString::fromStdString(args.dump()).left(300));
                    r = callTool(name, args);
                }
                const bool bad = r.value("isError", false);
                if(bad && failed < 0) failed = int(i);
                bool first = true;
                for(const json &item : r.value("content", json::array())) {
                    if(item.value("type", "") == "text") {
                        content.push_back({{"type", "text"},
                                           {"text", (first ? head + (bad ? ": FAILED: " : ": ") : std::string()) +
                                                        item.value("text", "")}});
                        first = false;
                    } else {
                        content.push_back(item);
                    }
                }
                if(first) content.push_back({{"type", "text"}, {"text", head + (bad ? ": FAILED" : ": done")}});
            }
            json result = {{"content", content}};
            if(failed >= 0) {
                content.push_back({{"type", "text"}, {"text", "call " + std::to_string(failed + 1) + " failed" +
                                                                   (stop ? "; the rest were skipped" : "")}});
                result = {{"content", content}, {"isError", true}};
            }
            return result;
        });
}

} // namespace cadjitsu
