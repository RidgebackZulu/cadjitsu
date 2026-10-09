#include "selftest/Showcase.h"

#include "MainWindow.h"
#include "command/CanvasCommands.h"
#include "command/CanvasValueBox.h"
#include "command/Command.h"
#include "command/CommandPanel.h"
#include "image/LensModel.h"
#include "mcp/McpButton.h"
#include "mcp/McpDialog.h"
#include "mcp/McpServer.h"
#include "mcp/McpTools.h"
#include "selftest/TestUtil.h"
#include "sketch/SketchEditor.h"
#include "sketch/SketchMode.h"
#include "sketch/SketchPalette.h"
#include "viewport/Camera.h"
#include "viewport/Viewport.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFontMetricsF>
#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTextStream>
#include <QTimer>
#include <QTransform>
#include <QTreeWidget>
#include <QVector2D>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cadjitsu {

namespace {

using json = nlohmann::json;

void sendMouse(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent ev(type, pos, w->mapToGlobal(pos), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &ev);
}

void clickAt(QWidget *w, QPointF p) {
    sendMouse(w, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton);
    sendMouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton);
    sendMouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton);
}

void dragAt(QWidget *w, QPointF from, QPointF to) {
    sendMouse(w, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton);
    sendMouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for(int i = 1; i <= 6; ++i) sendMouse(w, QEvent::MouseMove, from + (to - from) * (i / 6.0), Qt::NoButton, Qt::LeftButton);
    sendMouse(w, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

// A rounded label on a picture.
void label(QPainter &p, QPointF at, const QString &text) {
    QFont f = p.font();
    f.setPixelSize(30);
    f.setBold(true);
    p.setFont(f);
    const QRectF r = QFontMetricsF(f).boundingRect(text).adjusted(-18, -9, 18, 9);
    const QRectF box(at, r.size());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 28, 40, 205));
    p.drawRoundedRect(box, box.height() / 2, box.height() / 2);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, text);
}

// Pictures side by side at one height, a gap between them, each labelled.
QImage sideBySide(const std::vector<std::pair<QImage, QString>> &parts, int height, int gap = 16) {
    std::vector<QImage> scaled;
    int width = 0;
    for(const auto &[img, text] : parts) {
        scaled.push_back(img.scaledToHeight(height, Qt::SmoothTransformation));
        width += scaled.back().width();
    }
    width += gap * int(parts.size() - 1);
    QImage out(width, height, QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    int x = 0;
    for(size_t i = 0; i < scaled.size(); ++i) {
        p.drawImage(x, 0, scaled[i]);
        if(!parts[i].second.isEmpty()) label(p, QPointF(x + 22, 22), parts[i].second);
        x += scaled[i].width() + gap;
    }
    return out;
}

// A soft shadow under a shape: the shape drawn wider and fainter, again and again.
void softShadow(QPainter &p, const QPainterPath &shape, QPointF offset, double blur, int alpha) {
    p.save();
    p.translate(offset);
    p.setBrush(Qt::NoBrush);
    for(int i = 8; i >= 1; --i) {
        p.setPen(QPen(QColor(0, 0, 0, alpha / 8), blur * i / 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(shape);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, alpha));
    p.drawPath(shape);
    p.restore();
}

class Showcase {
public:
    Showcase(MainWindow &w, const QDir &out, QTextStream &log) : m_w(w), m_out(out), m_log(log) {}

    bool run() {
        m_w.resize(1600, 1000);
        processEventsFor(200);
        workshop();      // modeling.png, hero.jpg
        printChecks();   // print_checks.png
        sketch2d();      // sketch.png
        photoToSketch(); // photo_to_sketch.jpg
        viewsTo3d();     // views_to_3d.png
        agent();         // ai_agent.png
        return m_ok;
    }

private:
    MainWindow &m_w;
    const QDir &m_out;
    QTextStream &m_log;
    bool m_ok = true;
    // Set while an agent drives the app over HTTP (the MCP server): tool calls go there.
    std::function<json(const std::string &, const json &)> m_remote;

    Viewport *vp() const { return m_w.viewport(); }

    void check(bool cond, const QString &what) {
        m_log << (cond ? "  ok   " : "  FAIL ") << what << "\n";
        m_log.flush();
        m_ok &= cond;
    }

    // An MCP tool call, as an agent makes it; errors are logged and fail the run.
    json call(const std::string &name, const json &args = json::object()) {
        if(m_remote) return m_remote(name, args);
        const json r = m_w.mcpTools()->callTool(name, args);
        const std::string text = r.contains("content") ? r["content"][0].value("text", "") : r.dump();
        if(r.value("isError", false)) {
            m_log << "  FAIL " << QString::fromStdString(name) << ": " << QString::fromStdString(text) << "\n";
            m_ok = false;
            return json::object();
        }
        const json j = json::parse(text, nullptr, false);
        return j.is_discarded() ? json::object() : j;
    }

    // The planar face of `body` facing `normal` nearest `near`.
    json face(const std::string &body, const std::string &normal, const json &near) {
        const json faces = call("list_faces", {{"body", body}, {"normal", normal}, {"near", near}, {"limit", 1}}).value("faces", json::array());
        if(faces.empty()) return json::object();
        return {{"body", body}, {"index", faces[0]["index"]}};
    }
    json faceOfType(const std::string &body, const std::string &type, const json &near) {
        const json faces = call("list_faces", {{"body", body}, {"type", type}, {"near", near}, {"limit", 1}}).value("faces", json::array());
        if(faces.empty()) return json::object();
        return {{"body", body}, {"index", faces[0]["index"]}};
    }
    json edges(const std::string &body, json filter) {
        filter["body"] = body;
        json out = json::array();
        for(const json &e : call("list_edges", filter).value("edges", json::array()))
            out.push_back({{"body", body}, {"index", e["index"]}});
        return out;
    }
    static int featureId(const json &r) { return r.contains("feature") ? r["feature"].value("id", 0) : 0; }
    // The newest body's name (bodies are listed oldest first).
    std::string lastBody() {
        const json bodies = call("get_design").value("bodies", json::array());
        return bodies.empty() ? std::string() : bodies.back().value("name", std::string());
    }

    void settle(int frames = 3) {
        m_w.waitForModel(120000);
        waitForFrames(vp(), frames);
        processEventsFor(50);
    }
    QImage window() {
        settle();
        return m_w.grab().toImage();
    }
    // The canvas part of the window (no ribbon, browser or timeline).
    QImage canvas() {
        settle();
        return m_w.grab(QRect(vp()->mapTo(&m_w, QPoint(0, 0)), vp()->size())).toImage();
    }
    void save(QImage img, const QString &name) {
        if(img.width() > 1800) img = img.scaledToWidth(1800, Qt::SmoothTransformation);
        const bool ok = !img.isNull() && (name.endsWith(QStringLiteral(".jpg")) ? img.save(m_out.filePath(name), "JPG", 92)
                                                                                : img.save(m_out.filePath(name)));
        check(ok, QStringLiteral("saved %1 (%2 x %3)").arg(name).arg(img.width()).arg(img.height()));
    }

    // The bodies' bounding box (world mm).
    Box3 bodiesBox() {
        Box3 box;
        for(const json &b : call("get_design").value("bodies", json::array())) {
            const json bb = b.value("bounding_box", json::object());
            if(!bb.contains("min") || !b.value("visible", true)) continue;
            box.add(QVector3D(bb["min"][0].get<float>(), bb["min"][1].get<float>(), bb["min"][2].get<float>()));
            box.add(QVector3D(bb["max"][0].get<float>(), bb["max"][1].get<float>(), bb["max"][2].get<float>()));
        }
        return box;
    }
    // Looks along `dir` (from the eye to the model) at `box` (default: the
    // bodies), filling the view (margin < 1 crops in); `shift` moves the
    // picture by fractions of the view height.
    void look(const QVector3D &dir, float margin = 1.0f, QVector2D shift = {}, Box3 box = {}) {
        Camera &cam = vp()->camera();
        cam.setOrientation(dir.normalized(), QVector3D(0, 0, 1));
        if(box.isEmpty()) box = bodiesBox();
        if(box.isEmpty()) vp()->fitAll(false);
        else cam.fit(box, margin);
        if(!shift.isNull()) cam.target -= (cam.right() * shift.x() + cam.up() * shift.y()) * cam.viewHeightAtTarget();
        vp()->update();
        settle();
    }
    static int samples() {
        bool ok = false;
        const int n = qEnvironmentVariableIntValue("CADJITSU_SHOWCASE_SAMPLES", &ok);
        return ok && n > 0 ? n : 16;
    }
    // A path-traced picture of the current view.
    QImage render(int width, int height) {
        QElapsedTimer clock;
        clock.start();
        const QString png = m_out.filePath(QStringLiteral("showcase_render.tmp.png"));
        const json r = call("render_image", {{"path", png.toStdString()}, {"width", width}, {"height", height}, {"samples", samples()}});
        const QImage img(png);
        QFile::remove(png);
        m_log << "         path traced " << width << " x " << height << ", " << r.value("samples", 0) << " samples in "
              << clock.elapsed() / 1000.0 << " s\n";
        return img;
    }

    // --- Parts --------------------------------------------------------------------------------

    // An electronics case: rounded, shelled, with standoffs, a USB-C port,
    // vents and an engraved name. Returns the body's name.
    std::string enclosure() {
        const json sk = call("create_sketch", {{"plane", "XY"}, {"name", "Case"},
                                               {"entities", {{{"type", "center_rectangle"}, {"center", {0, 0}}, {"width", 90}, {"height", 60}}}}});
        call("extrude", {{"sketch", sk.value("sketch", 0)}, {"distance", 32}});
        const std::string body = lastBody();
        call("fillet", {{"edges", edges(body, {{"direction", "z"}})}, {"radius", 8}});
        call("shell", {{"faces", {face(body, "+z", {0, 0, 32})}}, {"thickness", 2.4}});
        call("chamfer", {{"faces", {face(body, "-z", {0, 0, 0})}}, {"distance", 0.8}});
        // Four standoffs for the board, hollow for self-tapping screws.
        json rings = json::array(), inside = json::array();
        for(int sx : {-1, 1})
            for(int sy : {-1, 1}) {
                rings.push_back({{"type", "circle"}, {"center", {sx * 35, sy * 20}}, {"radius", 3.6}});
                rings.push_back({{"type", "circle"}, {"center", {sx * 35, sy * 20}}, {"radius", 1.3}});
                inside.push_back({sx * 35 + 2.4, sy * 20});
            }
        const json st = call("create_sketch", {{"plane", "XY"}, {"name", "Standoffs"}, {"entities", rings}});
        call("extrude", {{"sketch", st.value("sketch", 0)}, {"profile_points", inside}, {"distance", 12}, {"operation", "join"}});
        // A USB-C port in the front and vent slots down the side.
        const json usb = call("create_sketch", {{"plane", "XZ"}, {"name", "USB-C"},
                                                {"entities", {{{"type", "slot"}, {"start", {-4.5, 11}}, {"end", {4.5, 11}}, {"width", 3.4}}}}});
        call("extrude", {{"sketch", usb.value("sketch", 0)}, {"distance", 40}, {"operation", "cut"}});
        const json vent = call("create_sketch", {{"plane", "YZ"}, {"name", "Vent"},
                                                 {"entities", {{{"type", "slot"}, {"start", {-16, 15}}, {"end", {-16, 26}}, {"width", 3}}}}});
        const json cut = call("extrude", {{"sketch", vent.value("sketch", 0)}, {"distance", 50}, {"operation", "cut"}});
        call("pattern", {{"type", "rectangular"}, {"features", {featureId(cut)}}, {"direction", "y"}, {"count", 5}, {"spacing", 8}});
        call("emboss_text", {{"face", face(body, "-y", {0, -30, 22})}, {"text", "CADJITSU"}, {"at", {0, -30, 23}},
                             {"size", 5}, {"bold", true}, {"depth", 0.6}});
        return body;
    }

    // The case's lid, `height` above the plate, with countersunk screw holes.
    std::string lid(double height) {
        const int plane = featureId(call("offset_plane", {{"base", "XY"}, {"offset", height}}));
        const json sk = call("create_sketch", {{"plane", {{"plane", plane}}}, {"name", "Lid"},
                                               {"entities", {{{"type", "center_rectangle"}, {"center", {0, 0}}, {"width", 90}, {"height", 60}}}}});
        call("extrude", {{"sketch", sk.value("sketch", 0)}, {"distance", 3}});
        const std::string body = lastBody();
        const double top = height + 3;
        call("fillet", {{"edges", edges(body, {{"direction", "z"}})}, {"radius", 8}});
        call("fillet", {{"faces", {face(body, "+z", {0, 0, top})}}, {"radius", 1.2}});
        json points = json::array();
        for(int sx : {-1, 1})
            for(int sy : {-1, 1}) points.push_back({sx * 35, sy * 20, top});
        call("hole", {{"face", face(body, "+z", {0, 0, top})}, {"points", points}, {"type", "countersink"}, {"diameter", 3.2},
                      {"csink_diameter", 6.4}, {"through_all", true}});
        return body;
    }

    // A vase turned about a vertical axis at (x0, y0), then hollowed.
    std::string vase(double x0, double y0) {
        // On a plane parallel to XZ through the axis: the profile's x is world X, its y world Z.
        const int plane = featureId(call("offset_plane", {{"base", "XZ"}, {"offset", -y0}}));
        // A round belly (centre (2, 18) from the axis) into a flared neck, the
        // two arcs meeting exactly on both circles.
        const double jx = 2 + std::sqrt(45.0), jz = 38;              // on the belly circle
        const double nx = (208 - jx * jx) / (24 - 2 * jx), nz = 44; // the neck's centre
        const json sk = call("create_sketch",
                             {{"plane", {{"plane", plane}}},
                              {"name", "Vase profile"},
                              {"entities",
                               {{{"type", "line"}, {"from", {x0, 0}}, {"to", {x0 + 13, 0}}},
                                {{"type", "arc"}, {"center", {x0 + 2, 18}}, {"start", {x0 + 13, 0}}, {"end", {x0 + jx, jz}}},
                                {{"type", "arc"}, {"center", {x0 + nx, nz}}, {"start", {x0 + 12, 54}}, {"end", {x0 + jx, jz}}},
                                {{"type", "line"}, {"from", {x0 + 12, 54}}, {"to", {x0, 54}}},
                                {{"type", "line"}, {"from", {x0, 54}}, {"to", {x0, 0}}}}}});
        int axis = 0;
        for(const json &c : sk.value("curves", json::array()))
            if(c.value("type", "") == "line" && std::abs(c["from"][0].get<double>() - x0) < 1e-6 &&
               std::abs(c["to"][0].get<double>() - x0) < 1e-6)
                axis = c["id"];
        call("revolve", {{"sketch", sk.value("sketch", 0)}, {"axis", {{"sketch_line", {sk.value("sketch", 0), axis}}}}});
        const std::string body = lastBody();
        call("shell", {{"faces", {face(body, "+z", {x0, y0, 54})}}, {"thickness", 1.6}});
        return body;
    }

    // A knurled knob with a hex nut trap.
    std::string knob(double gx, double gy) {
        const json hub = call("create_sketch", {{"plane", "XY"}, {"name", "Knob"},
                                                {"entities", {{{"type", "circle"}, {"center", {gx, gy}}, {"radius", 14}}}}});
        call("extrude", {{"sketch", hub.value("sketch", 0)}, {"distance", 12}});
        const std::string body = lastBody();
        const json tooth = call("create_sketch",
                                {{"plane", "XY"},
                                 {"name", "Grip"},
                                 {"entities", {{{"type", "polygon"},
                                                {"points", {{gx + 13.2, gy - 2.4}, {gx + 18.5, gy - 1.5}, {gx + 18.5, gy + 1.5}, {gx + 13.2, gy + 2.4}}}}}}});
        const json t = call("extrude", {{"sketch", tooth.value("sketch", 0)}, {"distance", 12}, {"operation", "join"}});
        call("pattern", {{"type", "circular"}, {"features", {featureId(t)}}, {"axis", {{"face", faceOfType(body, "cylinder", {gx - 14, gy, 6})}}},
                         {"count", 16}});
        const json hex = call("create_sketch", {{"plane", "XY"}, {"name", "Nut trap"},
                                                {"entities", {{{"type", "regular_polygon"}, {"center", {gx, gy}}, {"sides", 6}, {"diameter", 8.1}, {"inscribed", false}}}}});
        call("extrude", {{"sketch", hex.value("sketch", 0)}, {"distance", 12}, {"operation", "cut"}});
        call("fillet", {{"faces", {face(body, "+z", {gx, gy, 12})}}, {"radius", 0.8}});
        return body;
    }

    // An M8 bolt with a modelled thread, standing on its head.
    std::string bolt(double bx, double by) {
        const json head = call("create_sketch", {{"plane", "XY"}, {"name", "Bolt head"},
                                                 {"entities", {{{"type", "regular_polygon"}, {"center", {bx, by}}, {"sides", 6}, {"diameter", 13}, {"inscribed", false}}}}});
        call("extrude", {{"sketch", head.value("sketch", 0)}, {"distance", 5.5}});
        const std::string body = lastBody();
        call("chamfer", {{"faces", {face(body, "+z", {bx, by, 5.5})}}, {"distance", 0.6}});
        const json shank = call("create_sketch", {{"plane", "XY"}, {"name", "Bolt shank"},
                                                  {"entities", {{{"type", "circle"}, {"center", {bx, by}}, {"radius", 4}}}}});
        call("extrude", {{"sketch", shank.value("sketch", 0)}, {"distance", 34}, {"operation", "join"}});
        call("thread", {{"faces", {faceOfType(body, "cylinder", {bx + 4, by, 22})}}, {"size", "M8"}, {"mode", "modeled"}, {"length", 24}});
        return body;
    }

    // An M8 nut with a modelled thread, its bottom `z` above the plate.
    std::string nut(double nx, double ny, double z) {
        const int plane = featureId(call("offset_plane", {{"base", "XY"}, {"offset", z}}));
        // The plane's frame sits over the world origin: its x and y are world X and Y.
        const json sk2 = call("create_sketch", {{"plane", {{"plane", plane}}}, {"name", "Nut"},
                                                {"entities", {{{"type", "regular_polygon"}, {"center", {nx, ny}}, {"sides", 6}, {"diameter", 13}, {"inscribed", false}}}}});
        call("extrude", {{"sketch", sk2.value("sketch", 0)}, {"distance", 6.5}});
        const std::string body = lastBody();
        call("hole", {{"face", face(body, "+z", {nx, ny, z + 6.5})}, {"points", {{nx, ny, z + 6.5}}}, {"type", "tapped"}, {"thread", "M8"},
                      {"thread_mode", "modeled"}, {"through_all", true}});
        return body;
    }

    // --- Scenes -------------------------------------------------------------------------------

    // A small workshop: the case and its lid, a vase, a knob and a bolt.
    void workshop() {
        m_w.newDocument();
        const std::string box = enclosure(), cover = lid(46), pot = vase(76, 52), grip = knob(-66, -48), screw = bolt(92, -22);
        call("set_material", {{"bodies", {box}}, {"material", "PLA"}, {"finish", "matte"}, {"color", "Snow White"}});
        call("set_material", {{"bodies", {cover}}, {"material", "PETG"}, {"finish", "semitransparent"}, {"color", "Ice Blue"}});
        call("set_material", {{"bodies", {pot}}, {"material", "PLA"}, {"finish", "silk"}, {"color", "Silk Gold"}});
        call("set_material", {{"bodies", {grip}}, {"material", "PLA"}, {"finish", "matte"}, {"color", "Orange"}});
        call("set_material", {{"bodies", {screw}}, {"material", "PLA"}, {"finish", "silk"}, {"color", "Silk Silver"}});
        check(call("get_design").value("bodies", json::array()).size() == 5, QStringLiteral("the workshop: five bodies"));

        // In the app: the model, its browser and its timeline.
        call("set_display_style", {{"style", "shaded_edges"}});
        call("set_visibility", {{"folders", {"construction"}}, {"visible", false}});
        look(QVector3D(-0.62f, 0.68f, -0.40f), 0.80f);
        save(window(), QStringLiteral("modeling.png"));

        // Path traced on the build plate.
        call("set_render", {{"plate", "textured_pei"}, {"lighting", "studio"}, {"placement", "centered"}, {"layer_lines", true},
                            {"ray_traced", false}, {"quality", "final"}});
        look(QVector3D(-0.60f, 0.74f, -0.27f), 0.70f, QVector2D(0.0f, 0.06f));
        save(render(1800, 900), QStringLiteral("hero.jpg"));
        call("set_display_style", {{"style", "shaded_edges"}});
    }

    // Overhangs to support, and a section through a nut on a bolt.
    void printChecks() {
        m_w.newDocument();
        vase(-45, 0);
        // An arch with a shelf: its ceiling and the shelf's underside overhang.
        const json block = call("create_sketch", {{"plane", "XZ"}, {"name", "Arch"},
                                                  {"entities", {{{"type", "rectangle"}, {"corner1", {5, 0}}, {"corner2", {75, 42}}},
                                                                {{"type", "circle"}, {"center", {40, 0}}, {"radius", 24}}}}});
        call("extrude", {{"sketch", block.value("sketch", 0)}, {"profile_points", {{10, 38}}}, {"direction", "symmetric"}, {"distance", 10}});
        const json shelf = call("create_sketch", {{"plane", "XZ"}, {"name", "Shelf"},
                                                  {"entities", {{{"type", "rectangle"}, {"corner1", {75, 30}}, {"corner2", {100, 36}}}}}});
        call("extrude", {{"sketch", shelf.value("sketch", 0)}, {"direction", "symmetric"}, {"distance", 10}, {"operation", "join"}});
        call("set_display_style", {{"style", "shaded_edges"}});
        call("set_visibility", {{"folders", {"construction"}}, {"visible", false}});
        // Seen from below, in front and to the left, to show the undersides.
        look(QVector3D(0.42f, 0.80f, 0.42f), 0.78f);
        m_w.action(QStringLiteral("overhangs"))->trigger();
        settle(4);
        const QImage overhangs = canvas();
        if(m_w.commandPanel() && m_w.commandPanel()->okButton()) m_w.commandPanel()->okButton()->click();

        // A nut on a bolt, cut through the middle: the threads mesh.
        m_w.newDocument();
        const std::string screw = bolt(0, 0), held = nut(0, 0, 14);
        call("set_material", {{"bodies", {screw}}, {"color", "Ash Grey"}});
        call("set_material", {{"bodies", {held}}, {"color", "Sky Blue"}});
        call("set_display_style", {{"style", "shaded_edges"}});
        call("set_visibility", {{"folders", {"construction"}}, {"visible", false}});
        call("section", {{"plane", "XZ"}, {"offset", 0}});
        look(QVector3D(-0.30f, 0.92f, -0.26f), 1.08f);
        // Without the section's depth handle, which sits in the middle of it.
        ViewportTool *handle = vp()->activeTool();
        vp()->setIdleTool(nullptr);
        for(auto *box : vp()->findChildren<CanvasValueBox *>()) box->hide();
        const QImage section = canvas();
        vp()->setIdleTool(handle);
        call("section", {{"hide", true}});
        save(sideBySide({{overhangs, QStringLiteral("Overhang analysis")}, {section, QStringLiteral("Section through printed M8 threads")}}, 760),
             QStringLiteral("print_checks.png"));
    }

    // A mounting plate drawn in a sketch, with the Circular Pattern dialog open.
    void sketch2d() {
        m_w.newDocument();
        const json entities = {
            {{"type", "center_rectangle"}, {"center", {0, 0}}, {"width", 120}, {"height", 80}},
            {{"type", "circle"}, {"center", {0, 0}}, {"diameter", 30}},
            {{"type", "circle"}, {"center", {0, 0}}, {"radius", 24}, {"construction", true}, {"dimension", false}},
            {{"type", "circle"}, {"center", {0, 24}}, {"diameter", 6}},
            {{"type", "slot"}, {"start", {-44, -28}}, {"end", {-26, -28}}, {"width", 8}},
            {{"type", "slot"}, {"start", {26, -28}}, {"end", {44, -28}}, {"width", 8}},
            {{"type", "regular_polygon"}, {"center", {-40, 22}}, {"sides", 6}, {"diameter", 10}, {"inscribed", false}},
            {{"type", "regular_polygon"}, {"center", {40, 22}}, {"sides", 6}, {"diameter", 10}, {"inscribed", false}}};
        const json sk = call("create_sketch", {{"plane", "XY"}, {"name", "Mounting plate"}, {"entities", entities}});
        const int id = sk.value("sketch", 0);
        for(int sx : {-1, 1})
            for(int sy : {-1, 1}) call("sketch_fillet", {{"sketch", id}, {"near", {sx * 60, sy * 40}}, {"radius", 10}});
        SketchMode *mode = m_w.sketchMode();
        check(mode->editSketch(id, false), QStringLiteral("the plate's sketch opens for editing"));
        SketchEditor *ed = mode->editor();
        settle();
        vp()->fitAll(false);
        vp()->camera().zoom(1.25f, vp()->camera().target);
        vp()->camera().target += vp()->camera().right() * (0.10f * vp()->camera().viewHeightAtTarget());
        settle();
        // Six holes round the bore: pick the hole, the centre, type the count.
        m_w.action(QStringLiteral("sketchCircularPattern"))->trigger();
        CommandPanel *panel = m_w.commandPanel();
        dragAt(vp(), ed->toScreen({-5, 19}), ed->toScreen({5, 29}));
        if(auto *centre = panel->findChild<SelectionField *>(QStringLiteral("sketchPatternCentre"))) emit centre->activated();
        clickAt(vp(), ed->toScreen({0, 0}));
        if(auto *count = panel->findChild<ValueField *>(QStringLiteral("sketchPatternCount"))) count->setExpression(QStringLiteral("6"));
        sendMouse(vp(), QEvent::MouseMove, ed->toScreen({30, 52}), Qt::NoButton, Qt::NoButton);
        check(!ed->previewLines.empty() || panel->okButton()->isEnabled(), QStringLiteral("the pattern previews"));
        // Dimensions shown, the constraint glyphs hidden (Sketch Palette).
        QCheckBox *glyphs = m_w.findChild<QCheckBox *>(QStringLiteral("paletteConstraints"));
        const bool glyphsWere = glyphs && glyphs->isChecked();
        if(glyphs) glyphs->setChecked(false);
        save(window(), QStringLiteral("sketch.png"));
        if(glyphs) glyphs->setChecked(glyphsWere);
        panel->okButton()->click();
        mode->finish();
        settle();
    }

    // The photo: a part on an A4 sheet on a wooden table, taken at an angle
    // through a phone's wide lens. `sheet` gets the sheet's corners (as
    // drawn, before the lens) and `toPhoto` maps sheet mm to the photo.
    QImage tablePhoto(const cad::LensDistortion &lens, QPolygonF &sheet) {
        const int W = 1600, H = 1200;
        sheet = QPolygonF({QPointF(395, 235), QPointF(1215, 250), QPointF(1435, 1015), QPointF(165, 1030)});
        QImage img(W, H, QImage::Format_ARGB32);
        QRandomGenerator rng(20261009);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            // Oak: a warm gradient with long grain.
            QLinearGradient wood(0, 0, W, H);
            wood.setColorAt(0, QColor(160, 116, 76));
            wood.setColorAt(0.5, QColor(141, 99, 62));
            wood.setColorAt(1, QColor(118, 80, 48));
            p.fillRect(img.rect(), wood);
            for(int i = 0; i < 140; ++i) {
                const double y = rng.bounded(H + 400) - 200.0, amp = 6 + rng.bounded(18), phase = rng.bounded(628) / 100.0;
                QPainterPath grain;
                grain.moveTo(-20, y);
                for(int x = 0; x <= W + 40; x += 40) grain.lineTo(x, y + 0.08 * x + amp * std::sin(x / 210.0 + phase));
                const int shade = rng.bounded(2) ? 70 : 185;
                p.setPen(QPen(QColor(shade, int(shade * 0.72), int(shade * 0.45), 22 + int(rng.bounded(30))), 1.0 + rng.bounded(30) / 10.0));
                p.drawPath(grain);
            }
            // The sheet, its shadow first.
            QPainterPath paper;
            paper.addPolygon(sheet);
            paper.closeSubpath();
            softShadow(p, paper, QPointF(10, 14), 10, 70);
            QLinearGradient light(sheet[0], sheet[2]);
            light.setColorAt(0, QColor(250, 250, 247));
            light.setColorAt(1, QColor(232, 232, 226));
            p.setPen(Qt::NoPen);
            p.setBrush(light);
            p.drawPath(paper);
            // On the sheet (in mm, y down): a bike-light bracket and a ruler.
            QTransform onSheet;
            QTransform::quadToQuad(QPolygonF({QPointF(0, 0), QPointF(297, 0), QPointF(297, 210), QPointF(0, 210)}), sheet, onSheet);
            p.setTransform(onSheet);
            QPainterPath body; // odd-even: the holes are holes
            body.setFillRule(Qt::OddEvenFill);
            body.addRoundedRect(QRectF(63, 77, 170, 56), 28, 28);
            body.addEllipse(QPointF(91, 105), 12, 12);
            body.addRoundedRect(QRectF(130, 100, 50, 10), 5, 5);
            body.addEllipse(QPointF(207, 93), 3.2, 3.2);
            body.addEllipse(QPointF(207, 117), 3.2, 3.2);
            softShadow(p, body, QPointF(1.6, 2.2), 1.6, 80);
            QLinearGradient metal(0, 77, 0, 133);
            metal.setColorAt(0, QColor(74, 84, 100));
            metal.setColorAt(0.5, QColor(58, 66, 80));
            metal.setColorAt(1, QColor(48, 55, 68));
            p.setBrush(metal);
            p.setPen(QPen(QColor(36, 41, 51), 0.5));
            p.drawPath(body);
            // A light catching the top edges.
            p.save();
            p.setClipPath(body);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(92, 104, 122), 0.7));
            p.translate(0.35, 0.45);
            p.drawPath(body);
            p.restore();
            p.setPen(QPen(QColor(40, 40, 40), 0.45));
            for(int x = 20; x <= 277; x += 5) p.drawLine(QPointF(x, 198), QPointF(x, x % 50 == 0 ? 186 : x % 10 == 0 ? 190 : 193));
            p.resetTransform();
            // A soft light falling off to the corners.
            QRadialGradient vignette(QPointF(W * 0.55, H * 0.45), W * 0.75);
            vignette.setColorAt(0.6, QColor(0, 0, 0, 0));
            vignette.setColorAt(1, QColor(0, 0, 0, 90));
            p.fillRect(img.rect(), vignette);
        }
        // Through the lens, with a little sensor noise.
        QImage photo(W, H, QImage::Format_ARGB32);
        for(int y = 0; y < H; ++y)
            for(int x = 0; x < W; ++x) {
                const cad::Vec2 real = cad::undistortPixel(lens, {x + 0.5, y + 0.5}, W, H) - cad::Vec2(0.5, 0.5);
                const QRgb c = img.pixel(std::clamp(int(std::lround(real.x)), 0, W - 1), std::clamp(int(std::lround(real.y)), 0, H - 1));
                const int n = int(rng.bounded(7)) - 3;
                photo.setPixel(x, y, qRgb(std::clamp(qRed(c) + n, 0, 255), std::clamp(qGreen(c) + n, 0, 255), std::clamp(qBlue(c) + n, 0, 255)));
            }
        return photo;
    }

    // A photo of a part becomes a sketch: lens and perspective corrected, then traced.
    void photoToSketch() {
        m_w.newDocument();
        const cad::LensDistortion barrel{-0.10, 0.0};
        QPolygonF sheet;
        const QImage photo = tablePhoto(barrel, sheet);
        const QString path = m_out.filePath(QStringLiteral("showcase_photo.tmp.png"));
        photo.save(path);
        auto seen = [&](QPointF p) {
            const cad::Vec2 d = cad::distortPixel(barrel, {p.x(), p.y()}, photo.width(), photo.height());
            return cad::Vec2(d.x, d.y);
        };
        if(!m_w.insertCanvas(path)) {
            check(false, QStringLiteral("the photo is inserted"));
            return;
        }
        CommandPanel *panel = m_w.commandPanel();
        if(auto *w = panel->findChild<ValueField *>(QStringLiteral("canvasWidth"))) w->setExpression(QStringLiteral("400 mm"));
        if(auto *o = panel->findChild<ValueField *>(QStringLiteral("canvasOpacity"))) o->setExpression(QStringLiteral("100"));
        settle();
        panel->okButton()->click();
        settle();
        QFile::remove(path);
        const json canvases = call("get_design").value("canvases", json::array());
        if(canvases.empty()) {
            check(false, QStringLiteral("the photo is a canvas"));
            return;
        }
        const int id = canvases[0].value("id", 0);
        // Lens: points along three edges of the sheet, as the photo shows them.
        m_w.correctCanvasLens(id);
        if(auto *cmd = qobject_cast<CanvasCalibrateCommand *>(m_w.commands()->command())) {
            settle();
            const cad::ReferenceImage r = *m_w.document().canvas(id);
            const int edgesOf[3][2] = {{0, 1}, {1, 2}, {3, 0}};
            for(const auto &e : edgesOf) {
                for(int i = 0; i <= 6; ++i) cmd->addPoint(r.toPlane(seen(sheet[e[0]] + (sheet[e[1]] - sheet[e[0]]) * (0.04 + 0.92 * i / 6))));
                cmd->nextLine();
            }
            settle();
            panel->okButton()->click();
            settle();
        }
        // Perspective: the sheet's corners, A4.
        m_w.correctCanvasPerspective(id);
        if(auto *cmd = qobject_cast<CanvasCalibrateCommand *>(m_w.commands()->command())) {
            settle();
            const cad::ReferenceImage r = *m_w.document().canvas(id);
            for(const QPointF &c : sheet) cmd->addPoint(r.toPlane({c.x(), c.y()}));
            cmd->widthField()->setExpression(QStringLiteral("297 mm"));
            cmd->heightField()->setExpression(QStringLiteral("210 mm"));
            settle();
            panel->okButton()->click();
            settle();
        }
        const cad::ReferenceImage *corrected = m_w.document().canvas(id);
        check(corrected && corrected->lens && corrected->perspective, QStringLiteral("the photo's lens and perspective are corrected"));
        // Trace the bracket into a sketch on the same plane.
        SketchMode *mode = m_w.sketchMode();
        if(!mode->beginNewSketch(cad::PlaneRef::origin(cad::PlaneRef::Kind::XY), false)) return;
        auto at = [](double sx, double sy) { return QVector3D(float(sx - 148.5), float(105 - sy), 0); };
        Box3 part;
        part.add(at(55, 70));
        part.add(at(241, 140));
        vp()->camera().setOrientation(QVector3D(0, 0, -1), QVector3D(0, 1, 0));
        vp()->camera().fit(part, 0.86f);
        vp()->update();
        settle();
        m_w.action(QStringLiteral("sketchTrace"))->trigger();
        const QPointF inside = vp()->camera().project(at(110, 120));
        sendMouse(vp(), QEvent::MouseMove, inside, Qt::NoButton, Qt::NoButton);
        clickAt(vp(), inside);
        m_w.action(QStringLiteral("sketchSelect"))->trigger();
        sendMouse(vp(), QEvent::MouseMove, QPointF(5, 5), Qt::NoButton, Qt::NoButton);
        int curves = 0;
        for(const auto &e : mode->editor()->sketch().entities) curves += e.isCurve() && !e.construction;
        check(curves >= 8, QStringLiteral("the bracket traces into %1 curves").arg(curves));
        SketchPalette *palette = m_w.findChild<SketchPalette *>();
        QCheckBox *shading = m_w.findChild<QCheckBox *>(QStringLiteral("paletteProfile"));
        const bool shadingWas = shading && shading->isChecked();
        if(shading) shading->setChecked(false);
        if(palette) palette->hide();
        const QImage traced = canvas();
        if(palette) palette->show();
        if(shading) shading->setChecked(shadingWas);
        mode->finish();
        settle();
        save(sideBySide({{photo, QStringLiteral("1  The photo")}, {traced, QStringLiteral("2  Traced to scale")}}, 820),
             QStringLiteral("photo_to_sketch.jpg"));
    }

    // Front, side and top pictures of an angle bracket set up as one projection,
    // each traced, and the part built from them.
    void viewsTo3d() {
        m_w.newDocument();
        struct View {
            const char *file;
            QSize size;
            double pxPerMm;
            QPointF origin; // where (0, 0) of the view (mm) is in the picture
            std::function<void(QPainterPath &, QPainterPath &)> shape; // outline, holes (view mm, y up)
        };
        auto rect = [](QPainterPath &p, double x0, double y0, double x1, double y1) { p.addRect(QRectF(x0, y0, x1 - x0, y1 - y0)); };
        const View views[3] = {
            {"front", {900, 700}, 8.0, {450, 590}, [&](QPainterPath &o, QPainterPath &h) {
                 rect(o, -40, 0, 40, 50);
                 h.addEllipse(QPointF(-20, 35), 4, 4);
                 h.addEllipse(QPointF(20, 35), 4, 4);
             }},
            {"side", {700, 640}, 7.0, {330, 540}, [&](QPainterPath &o, QPainterPath &) {
                 rect(o, -20, 0, 20, 6);
                 rect(o, 14, 0, 20, 50);
             }},
            {"top", {800, 620}, 6.0, {390, 300}, [&](QPainterPath &o, QPainterPath &h) {
                 rect(o, -40, -20, 40, 20);
                 h.addEllipse(QPointF(-20, -5), 4, 4);
                 h.addEllipse(QPointF(20, -5), 4, 4);
             }}};
        json files = json::object();
        for(const View &v : views) {
            QImage img(v.size, QImage::Format_ARGB32);
            img.fill(QColor(248, 248, 245));
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            // A drawing sheet's frame, inside the picture's edge (the edge is
            // what tells the background).
            p.setPen(QPen(QColor(150, 160, 178), 3));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(12, 12, v.size.width() - 24, v.size.height() - 24));
            p.translate(v.origin);
            p.scale(v.pxPerMm, -v.pxPerMm); // mm, y up
            QPainterPath outline, holes;
            v.shape(outline, holes);
            const QPainterPath solid = outline.simplified().subtracted(holes);
            QLinearGradient shade(0, 50, 0, -20);
            shade.setColorAt(0, QColor(76, 92, 116));
            shade.setColorAt(1, QColor(58, 70, 90));
            p.setBrush(shade);
            p.setPen(QPen(QColor(40, 48, 62), 0.25));
            p.drawPath(solid);
            p.end();
            const QString file = m_out.filePath(QStringLiteral("showcase_%1.tmp.png").arg(QString::fromLatin1(v.file)));
            img.save(file);
            files[v.file] = file.toStdString();
        }
        const json r = call("insert_views", {{"front", files["front"]}, {"side", files["side"]}, {"top", files["top"]}, {"measured", "x"}, {"size", 80}});
        for(const View &v : views) QFile::remove(QString::fromStdString(files[v.file].get<std::string>()));
        check(r.value("canvases", json::array()).size() == 3, QStringLiteral("three views on XZ, YZ and XY"));
        // Front outline through the depth, intersected with the side's L, then the top's holes.
        const json front = call("create_sketch", {{"plane", "XZ"}, {"name", "Front"}, {"entities", json::array()}});
        call("trace_canvas", {{"sketch", front.value("sketch", 0)}, {"canvas", "Front"}, {"point", {0, 20}}});
        call("extrude", {{"sketch", front.value("sketch", 0)}, {"profile_points", {{0, 20}}}, {"direction", "symmetric"}, {"distance", 20}});
        const json side = call("create_sketch", {{"plane", "YZ"}, {"name", "Side"}, {"entities", json::array()}});
        call("trace_canvas", {{"sketch", side.value("sketch", 0)}, {"canvas", "Side"}, {"point", {0, 3}}});
        call("extrude", {{"sketch", side.value("sketch", 0)}, {"direction", "symmetric"}, {"distance", 40}, {"operation", "intersect"}});
        const json top = call("create_sketch", {{"plane", "XY"}, {"name", "Top"}, {"entities", json::array()}});
        call("trace_canvas", {{"sketch", top.value("sketch", 0)}, {"canvas", "Top"}, {"point", {0, 5}}});
        call("extrude", {{"sketch", top.value("sketch", 0)}, {"profile_points", {{0, 5}}}, {"distance", 50}, {"operation", "intersect"}});
        const json bodies = call("get_design").value("bodies", json::array());
        check(bodies.size() == 1, QStringLiteral("one bracket from the three views"));
        if(!bodies.empty())
            m_log << "         bracket: " << bodies[0].value("volume_mm3", 0.0) << " mm3 (" << bodies[0].value("faces", 0) << " faces)\n";
        call("set_material", {{"color", "Signal Red"}});
        call("set_display_style", {{"style", "shaded_edges"}});
        // The pictures moved off the part, as a drawing's views: the front
        // behind it, the side to its left, the top under it.
        const std::map<std::string, int> planes = {
            {"Front", featureId(call("offset_plane", {{"base", "XZ"}, {"offset", -44}}))},
            {"Side", featureId(call("offset_plane", {{"base", "YZ"}, {"offset", -66}}))},
            {"Top", featureId(call("offset_plane", {{"base", "XY"}, {"offset", -0.5}}))}};
        for(const cad::ReferenceImage &c : std::vector<cad::ReferenceImage>(m_w.document().canvases())) {
            auto it = planes.find(c.name);
            if(it == planes.end() || !it->second) continue;
            cad::ReferenceImage moved = c;
            moved.plane = cad::PlaneRef::construction(it->second);
            moved.opacity = 1.0;
            m_w.document().updateCanvas(moved, true, "Move " + c.name);
        }
        call("set_visibility", {{"folders", {"sketches", "construction"}}, {"visible", false}});
        Box3 all;
        all.add(QVector3D(-66, -40, -1));
        all.add(QVector3D(48, 44, 62));
        look(QVector3D(-0.70f, 0.62f, -0.36f), 0.86f, {}, all);
        save(window(), QStringLiteral("views_to_3d.png"));
    }

    // An agent builds the case over MCP; the event log shows its calls.
    void agent() {
        m_w.newDocument();
        quint16 port = 0;
        {
            QTcpServer probe;
            probe.listen(QHostAddress::LocalHost, 0);
            port = probe.serverPort();
        }
        McpSettings s;
        s.enabled = true;
        s.port = port;
        s.token = McpSettings::generateToken();
        check(m_w.mcpServer()->apply(s), QStringLiteral("the MCP server listens"));
        QNetworkAccessManager net;
        net.setProxy(QNetworkProxy::NoProxy);
        QByteArray session;
        auto rpc = [&](const std::string &method, const json &params) {
            QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/mcp").arg(port)));
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            req.setRawHeader("Authorization", "Bearer " + s.token.toLatin1());
            if(!session.isEmpty()) req.setRawHeader("Mcp-Session-Id", session);
            QNetworkReply *reply = net.post(
                req, QByteArray::fromStdString(json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}}.dump()));
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(120000, &loop, &QEventLoop::quit);
            if(!reply->isFinished()) loop.exec();
            if(!reply->rawHeader("Mcp-Session-Id").isEmpty()) session = reply->rawHeader("Mcp-Session-Id");
            const QByteArray body = reply->readAll();
            reply->deleteLater();
            return json::parse(body.constData(), body.constData() + body.size(), nullptr, false);
        };
        const json init = rpc("initialize", {{"protocolVersion", "2025-06-18"}, {"clientInfo", {{"name", "AI agent"}, {"version", "1"}}}});
        check(init.contains("result"), QStringLiteral("an agent connects"));
        m_remote = [&](const std::string &name, const json &args) -> json {
            const json r = rpc("tools/call", {{"name", name}, {"arguments", args}});
            if(!r.contains("result")) {
                m_log << "  FAIL " << QString::fromStdString(name) << ": no answer\n";
                m_ok = false;
                return json::object();
            }
            const std::string text = r["result"]["content"][0].value("text", "");
            if(r["result"].value("isError", false)) {
                m_log << "  FAIL " << QString::fromStdString(name) << ": " << QString::fromStdString(text) << "\n";
                m_ok = false;
                return json::object();
            }
            const json j = json::parse(text, nullptr, false);
            return j.is_discarded() ? json::object() : j;
        };
        const std::string box = enclosure();
        call("set_material", {{"bodies", {box}}, {"material", "PLA"}, {"finish", "matte"}, {"color", "Grass Green"}});
        call("export_stl", {{"path", m_out.filePath(QStringLiteral("showcase_case.tmp.stl")).toStdString()}});
        m_remote = nullptr;
        QFile::remove(m_out.filePath(QStringLiteral("showcase_case.tmp.stl")));
        call("set_display_style", {{"style", "shaded_edges"}});
        look(QVector3D(-0.62f, 0.68f, -0.40f), 1.05f, QVector2D(-0.22f, 0.04f));
        processEventsFor(900); // the button's glow pulses
        const QImage main = window();
        McpDialog *dlg = m_w.openMcpDialog();
        processEventsFor(300);
        // The calls, without the details pane and the log file's path below them.
        const int listBottom = dlg->logView()->mapTo(dlg, QPoint(0, dlg->logView()->height())).y() + 14;
        const QImage log = dlg->grab().toImage().copy(0, 0, dlg->width(), listBottom);
        dlg->close();
        // The event log over the window, as on a desktop.
        QImage shot = main.convertToFormat(QImage::Format_RGB32);
        {
            QPainter p(&shot);
            p.setRenderHint(QPainter::Antialiasing);
            const QPointF at(shot.width() - log.width() - 36, shot.height() - log.height() - 96);
            QPainterPath frame;
            frame.addRoundedRect(QRectF(at, log.size()), 10, 10);
            softShadow(p, frame, QPointF(0, 10), 16, 60);
            p.setClipPath(frame);
            p.drawImage(at, log);
        }
        save(shot, QStringLiteral("ai_agent.png"));
        s.enabled = false;
        m_w.mcpServer()->apply(s);
    }
};

} // namespace

bool showcaseScenario(MainWindow &w, const QDir &out, QTextStream &log) { return Showcase(w, out, log).run(); }

} // namespace cadjitsu
