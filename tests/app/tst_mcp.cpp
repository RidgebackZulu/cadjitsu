// The MCP server: authentication and origin checks, the connection button,
// a whole part built through tools (sketch, extrude, fillet, hole, parameter
// change, export), errors reported to the agent, screenshots, the rolling
// event log file and the dialog.
#include "TestRegistry.h"

#include "MainWindow.h"
#include "features/ConstructionPlaneFeature.h"
#include "mcp/McpButton.h"
#include "mcp/McpDialog.h"
#include "mcp/McpLog.h"
#include "mcp/McpServer.h"
#include "model/ModelView.h"
#include "selftest/TestUtil.h"
#include "viewport/Viewport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPushButton>
#include <QSpinBox>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include <nlohmann/json.hpp>

using namespace cadjitsu;
using json = nlohmann::json;

namespace {

const QString kToken = QStringLiteral("test-token-abcdefghijklmnopqrstuvwxyz");

quint16 freePort() {
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    return s.serverPort();
}

} // namespace

class McpTests : public QObject {
    Q_OBJECT

    std::unique_ptr<MainWindow> m_window;
    std::unique_ptr<QTemporaryDir> m_dir;
    QNetworkAccessManager m_net;
    quint16 m_port = 0;
    QByteArray m_session;

    struct Reply {
        int status = 0;
        json body;
        QByteArray raw;
        QByteArray session;
    };

    Reply post(const QByteArray &body, const QString &token = kToken, const QByteArray &origin = {}, const QString &path = QStringLiteral("/mcp"),
               const QByteArray &verb = "POST") {
        QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_port).arg(path)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if(!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token.toLatin1());
        if(!origin.isEmpty()) req.setRawHeader("Origin", origin);
        if(!m_session.isEmpty()) req.setRawHeader("Mcp-Session-Id", m_session);
        QNetworkReply *r = verb == "POST" ? m_net.post(req, body) : m_net.sendCustomRequest(req, verb);
        QEventLoop loop;
        connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(120000, &loop, &QEventLoop::quit);
        if(!r->isFinished()) loop.exec();
        Reply out;
        out.status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.raw = r->readAll();
        out.session = r->rawHeader("Mcp-Session-Id");
        out.body = json::parse(out.raw.constData(), out.raw.constData() + out.raw.size(), nullptr, false);
        r->deleteLater();
        return out;
    }
    json rpc(const std::string &method, const json &params = json::object()) {
        const Reply r = post(QByteArray::fromStdString(json{{"jsonrpc", "2.0"}, {"id", 7}, {"method", method}, {"params", params}}.dump()));
        if(!r.session.isEmpty()) m_session = r.session;
        return r.body;
    }
    // A tool call: (isError, parsed text payload or raw text).
    std::pair<bool, json> tool(const std::string &name, const json &args = json::object()) {
        const json r = rpc("tools/call", {{"name", name}, {"arguments", args}});
        const json &res = r.at("result");
        const std::string text = res.at("content").at(0).value("text", "");
        json payload = json::parse(text, nullptr, false);
        return {res.value("isError", false), payload.is_discarded() ? json(text) : payload};
    }
    void initialize() {
        const json r = rpc("initialize", {{"protocolVersion", "2025-06-18"},
                                          {"clientInfo", {{"name", "cadjitsu-test"}, {"version", "1.0"}}},
                                          {"capabilities", json::object()}});
        QCOMPARE(QString::fromStdString(r["result"]["serverInfo"]["name"]), QStringLiteral("cadjitsu"));
    }

private slots:
    void initTestCase() { m_net.setProxy(QNetworkProxy::NoProxy); }

    void init() {
        m_dir = std::make_unique<QTemporaryDir>();
        m_window = std::make_unique<MainWindow>();
        m_window->mcpLog()->setDirectory(m_dir->filePath(QStringLiteral("logs")));
        m_window->resize(1300, 850);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QVERIFY(waitForFrames(m_window->viewport(), 2));
        m_port = freePort();
        m_session.clear();
        McpSettings s;
        s.enabled = true;
        s.port = m_port;
        s.token = kToken;
        QVERIFY(m_window->mcpServer()->apply(s));
        QCOMPARE(m_window->mcpServer()->state(), McpServer::State::Listening);
        QCOMPARE(m_window->mcpButton()->state(), McpServer::State::Listening);
    }
    void cleanup() {
        m_window.reset();
        m_dir.reset();
    }

    void refusesWithoutTheTokenOrFromAWebPage() {
        const QByteArray ping = R"({"jsonrpc":"2.0","id":1,"method":"ping"})";
        QCOMPARE(post(ping, QString()).status, 401);
        QCOMPARE(post(ping, QStringLiteral("wrong-token-wrong-token-wrong-token")).status, 401);
        QCOMPARE(post(ping, kToken, "http://evil.example").status, 403);
        QCOMPARE(post(ping, kToken, {}, QStringLiteral("/other")).status, 404);
        QCOMPARE(post(ping, kToken, {}, QStringLiteral("/mcp"), "GET").status, 405);
        QCOMPARE(post(ping, kToken, "http://localhost:3000").status, 200);
        QCOMPARE(post(ping).status, 200);
        int refused = 0;
        for(const McpEvent &e : m_window->mcpLog()->events()) refused += e.kind == McpEvent::Kind::Auth;
        QCOMPARE(refused, 3);
        QCOMPARE(m_window->mcpButton()->state(), McpServer::State::Listening); // nobody connected
    }

    void anAgentConnectingLightsTheButton() {
        initialize();
        QVERIFY(!m_session.isEmpty());
        QCOMPARE(m_window->mcpServer()->state(), McpServer::State::Connected);
        QCOMPARE(m_window->mcpButton()->state(), McpServer::State::Connected);
        QCOMPARE(m_window->mcpServer()->clientName(), QStringLiteral("cadjitsu-test 1.0"));
        QTRY_VERIFY(m_window->mcpButton()->glow() > 0.05); // it pulses
        const json tools = rpc("tools/list")["result"]["tools"];
        std::set<std::string> names;
        for(const auto &t : tools) {
            names.insert(t["name"]);
            QVERIFY(t.contains("inputSchema") && t.contains("description"));
        }
        for(const char *n : {"get_design", "create_sketch", "extrude", "fillet", "chamfer", "hole", "combine", "list_edges",
                             "list_faces", "set_parameter", "edit_feature", "undo", "screenshot", "export_stl", "export_step", "emboss_text", "set_material", "set_render", "render_image", "sketch_mirror", "sketch_pattern", "project_to_sketch", "set_construction"})
            QVERIFY2(names.count(n), n);
        // The session ends: back to listening.
        QCOMPARE(post({}, kToken, {}, QStringLiteral("/mcp"), "DELETE").status, 200);
        QCOMPARE(m_window->mcpButton()->state(), McpServer::State::Listening);
    }

    // After a restart (Apply with a new port or token) the agent keeps using its
    // old session id: its calls work and the button shows it is connected.
    void aResumedSessionAfterARestartCountsAsConnected() {
        initialize();
        const QByteArray old = m_session;
        McpSettings s = m_window->mcpServer()->settings();
        s.port = freePort();
        m_port = s.port;
        QVERIFY(m_window->mcpServer()->apply(s));
        QCOMPARE(m_window->mcpButton()->state(), McpServer::State::Listening);
        m_session = old;
        const auto [err, design] = tool("get_design");
        QVERIFY2(!err, design.dump().c_str());
        QCOMPARE(m_window->mcpServer()->state(), McpServer::State::Connected);
        QCOMPARE(m_window->mcpButton()->state(), McpServer::State::Connected);
        QCOMPARE(m_window->mcpServer()->clientName(), QStringLiteral("cadjitsu-test 1.0")); // remembered
    }

    // Connected and idle: the button does not repaint at all; a request makes it
    // pulse once, briefly, at a modest frame rate.
    void theButtonOnlyAnimatesBrieflyAfterActivity() {
        initialize();
        McpButton *b = m_window->mcpButton();
        QTRY_VERIFY_WITH_TIMEOUT(!b->animating(), 3000);
        QVERIFY(b->glow() > 0.05); // the steady glow
        int before = b->paintCount();
        QTest::qWait(1500);
        QCOMPARE(b->paintCount(), before);
        before = b->paintCount();
        for(int i = 0; i < 5; ++i) rpc("ping");
        QVERIFY(b->animating());
        QTRY_VERIFY_WITH_TIMEOUT(!b->animating(), 3000);
        const int frames = b->paintCount() - before;
        QVERIFY2(frames > 3 && frames <= 60, qPrintable(QString::number(frames)));
    }

    void setVisibilityHidesPlanesBodiesAndFolders() {
        initialize();
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {20, 20}}}}}})
                     .first);
        const auto [e1, ex] = tool("extrude", {{"sketch", 1}, {"distance", 5}});
        QVERIFY2(!e1, ex.dump().c_str());
        const auto [e2, pl] = tool("offset_plane", {{"base", "XY"}, {"offset", 10}});
        QVERIFY2(!e2, pl.dump().c_str());
        const std::string body = ex["bodies"][0]["name"];
        // A plane by name, a body by name, and a whole folder.
        auto [e3, vis] = tool("set_visibility", {{"planes", {"Plane1"}}, {"visible", false}});
        if(e3) { // the plane may be named after its feature id
            const json d = tool("get_design").second;
            std::tie(e3, vis) = tool("set_visibility", {{"planes", {d["construction_planes"][0]["id"]}}, {"visible", false}});
        }
        QVERIFY2(!e3, vis.dump().c_str());
        QCOMPARE(vis["construction_planes"][0]["visible"].get<bool>(), false);
        QVERIFY(m_window->modelView()->planeQuads().empty());
        const auto [e4, v2] = tool("set_visibility", {{"bodies", {body}}, {"folders", {"sketches"}}, {"visible", false}});
        QVERIFY2(!e4, v2.dump().c_str());
        QCOMPARE(v2["bodies"][0]["visible"].get<bool>(), false);
        QCOMPARE(v2["folders"]["sketches"].get<bool>(), false);
        const json d = tool("get_design").second;
        QCOMPARE(d["folders"]["sketches"].get<bool>(), false);
        QCOMPARE(d["bodies"][0]["visible"].get<bool>(), false);
        QCOMPARE(d["construction_planes"][0]["visible"].get<bool>(), false);
        // A bad name changes nothing.
        QVERIFY(tool("set_visibility", {{"bodies", {"NoSuchBody"}}, {"folders", {"construction"}}, {"visible", false}}).first);
        QVERIFY(m_window->document().folderVisible("construction"));
    }

    void offsetSketchShellsAnOutline() {
        initialize();
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {20, 10}}}}}})
                     .first);
        // Too big inwards: refused, nothing changes.
        const auto [e0, r0] = tool("offset_sketch", {{"sketch", 1}, {"near", {{20, 5}}}, {"distance", -6}});
        QVERIFY(e0);
        QVERIFY2(r0.dump().find("too big") != std::string::npos, r0.dump().c_str());
        // One point on the outline takes all of it; 2 mm inwards.
        const auto [e1, r1] = tool("offset_sketch", {{"sketch", 1}, {"near", {{20, 5}}}, {"distance", -2}});
        QVERIFY2(!e1, r1.dump().c_str());
        QCOMPARE(int(r1["offset_curves"].size()), 4);
        QCOMPARE(int(r1["profiles"].size()), 2);
        const std::string param = r1["parameter"]["name"];
        // The ring between them, extruded 5 mm.
        const auto [e2, ex] = tool("extrude", {{"sketch", 1}, {"profile_points", {{1, 1}}}, {"distance", 5}});
        QVERIFY2(!e2, ex.dump().c_str());
        QVERIFY(std::fabs(ex["bodies"][0]["volume_mm3"].get<double>() - (200.0 - 16.0 * 6.0) * 5.0) < 1e-2);
        // The wall is a parameter.
        const auto [e3, r3] = tool("set_parameter", {{"name", param}, {"expression", 3}});
        QVERIFY2(!e3, r3.dump().c_str());
        const json d = tool("get_design").second;
        QVERIFY2(std::fabs(d["bodies"][0]["volume_mm3"].get<double>() - (200.0 - 14.0 * 4.0) * 5.0) < 1e-2, d.dump().c_str());
        // A side point picks the side for an open line.
        QVERIFY(!tool("add_to_sketch", {{"sketch", 1}, {"entities", {{{"type", "line"}, {"from", {0, 30}}, {"to", {20, 30}}}}}})
                     .first);
        const auto [e4, r4] = tool("offset_sketch", {{"sketch", 1}, {"near", {{10, 30}}}, {"distance", 4}, {"side_point", {10, 20}}});
        QVERIFY2(!e4, r4.dump().c_str());
        QCOMPARE(int(r4["offset_curves"].size()), 1);
    }

    void offsetPlaneTiltsOnBothAxes() {
        initialize();
        auto normalOf = [&](int i) {
            const json d = tool("get_design").second;
            const json &n = d["construction_planes"][i]["normal"];
            return QVector3D(n[0].get<float>(), n[1].get<float>(), n[2].get<float>());
        };
        const auto [e1, r1] = tool("offset_plane", {{"base", "XY"}, {"offset", 20}, {"tilt_x", 30}, {"tilt_y", "15 deg"}});
        QVERIFY2(!e1, r1.dump().c_str());
        // X first, then the tilted Y: n = Ry'(15) Rx(30) z.
        const double ax = M_PI / 6, ay = M_PI / 12;
        const QVector3D want(float(std::sin(ay)),
                             float(-std::cos(ay) * std::sin(ax)), float(std::cos(ay) * std::cos(ax)));
        QVERIFY2((normalOf(0) - want).length() < 2e-3f, qPrintable(QString::fromStdString(tool("get_design").second.dump())));
        const json d = tool("get_design").second;
        QCOMPARE(d["construction_planes"][0]["center"][2].get<double>(), 20.0); // turned about its centre
        // The older angle + axis form still works: 90 about Y points the normal along +X.
        const auto [e2, r2] = tool("offset_plane", {{"base", "XY"}, {"angle", 90}, {"axis", "y"}});
        QVERIFY2(!e2, r2.dump().c_str());
        QVERIFY((normalOf(1) - QVector3D(1, 0, 0)).length() < 2e-3f);
        // Both tilts are parameters.
        const auto f = std::dynamic_pointer_cast<const cad::ConstructionPlaneFeature>(m_window->document().features().front());
        QVERIFY(f && f->angle.expr == "30 deg" && f->angleY.expr == "15 deg");
    }

    void measureBetweenBodiesFacesAndPoints() {
        initialize();
        for(double x : {0.0, 30.0}) {
            QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                            {"entities", {{{"type", "rectangle"}, {"corner1", {x, 0}}, {"corner2", {x + 10, 10}}}}}})
                         .first);
        }
        const auto [e0, ex] = tool("batch", {{"calls", {{{"tool", "extrude"}, {"arguments", {{"sketch", 1}, {"distance", 5}}}},
                                                       {{"tool", "extrude"}, {"arguments", {{"sketch", 2}, {"distance", 5}}}}}}});
        QVERIFY2(!e0, ex.dump().c_str());
        const json d = tool("get_design").second;
        const std::string b1 = d["bodies"][0]["name"], b2 = d["bodies"][1]["name"];
        const auto [e1, r] = tool("measure", {{"a", {{"body", b1}}}, {"b", {{"body", b2}}}});
        QVERIFY2(!e1, r.dump().c_str());
        QCOMPARE(r["distance"].get<double>(), 20.0);
        const auto [e2, pp] = tool("measure", {{"a", {{"point", {0, 0, 0}}}}, {"b", {{"point", {3, 4, 0}}}}});
        QVERIFY2(!e2, pp.dump().c_str());
        QCOMPARE(pp["distance"].get<double>(), 5.0);
        QCOMPARE(pp["delta"][1].get<double>(), 4.0);
        const auto [e3, one] = tool("measure", {{"a", {{"body", b1}}}});
        QVERIFY2(!e3, one.dump().c_str());
        QCOMPARE(one["properties"]["Volume"].get<double>(), 500.0);
        QVERIFY(tool("measure", {{"a", {{"body", b1}, {"face", 999}}}}).first);
    }

    void overhangsReportsWhatNeedsSupport() {
        initialize();
        // A cube on the plate: nothing to support.
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {10, 10}}}}}})
                     .first);
        QVERIFY(!tool("extrude", {{"sketch", 1}, {"distance", 10}}).first);
        const auto [e1, r1] = tool("overhangs", json::object());
        QVERIFY2(!e1, r1.dump().c_str());
        QVERIFY(r1["prints_without_support"].get<bool>());
        QCOMPARE(r1["bodies"][0]["on_plate_area"].get<double>(), 100.0);
        // A floating slab above it: its underside is a 20 x 20 bridge.
        QVERIFY(!tool("offset_plane", {{"base", "XY"}, {"offset", 20}}).first);
        QVERIFY(!tool("create_sketch", {{"plane", {{"plane", 3}}},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {20, 20}}}}}})
                     .first);
        QVERIFY(!tool("extrude", {{"sketch", 4}, {"distance", 2}}).first);
        const auto [e2, r2] = tool("overhangs", {{"threshold", 50}});
        QVERIFY2(!e2, r2.dump().c_str());
        QVERIFY(!r2["prints_without_support"].get<bool>());
        QCOMPARE(r2["bodies"][1]["bridge_area"].get<double>(), 400.0);
        QVERIFY(r2["bodies"][1]["worst_faces"].size() == 1);
        QVERIFY(tool("overhangs", {{"threshold", 95}}).first);
    }

    void splitBodyIntoPrintablePieces() {
        initialize();
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {40, 20}}}}}})
                     .first);
        QVERIFY(!tool("extrude", {{"sketch", 1}, {"distance", 10}}).first);
        QVERIFY(!tool("offset_plane", {{"base", "YZ"}, {"offset", 20}}).first);
        const auto [e1, r] = tool("split_body", {{"plane", {{"plane", 3}}}, {"pins", true}});
        QVERIFY2(!e1, r.dump().c_str());
        QCOMPARE(int(r["bodies"].size()), 2);
        const double hole = M_PI * 1.6 * 1.6 * 6;
        for(const json &b : r["bodies"]) QVERIFY(std::fabs(b["volume_mm3"].get<double>() - (4000 - 2 * hole)) < 0.01);
        // A plane that misses the bodies is an error, and nothing is added.
        QVERIFY(!tool("offset_plane", {{"base", "YZ"}, {"offset", 100}}).first);
        QVERIFY(tool("split_body", {{"plane", {{"plane", 5}}}}).first);
        QVERIFY(tool("split_body", json::object()).first);
    }

    void draftTapersABox() {
        initialize();
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {40, 20}}}}}})
                     .first);
        QVERIFY(!tool("extrude", {{"sketch", 1}, {"distance", 10}}).first);
        const json faces = tool("list_faces", {{"body", "Body1"}, {"normal", "+x"}}).second;
        const json edges = tool("list_edges", {{"body", "Body1"}, {"near", {40, 10, 0}}}).second;
        const auto [e1, r] = tool("draft", {{"faces", {{{"body", "Body1"}, {"index", faces["faces"][0]["index"]}}}},
                                            {"hinge", {{"body", "Body1"}, {"index", edges["edges"][0]["index"]}}},
                                            {"angle", 10}});
        QVERIFY2(!e1, r.dump().c_str());
        const double wedge = 0.5 * 10 * 10 * std::tan(10 * M_PI / 180.0) * 20;
        QVERIFY(std::fabs(r["bodies"][0]["volume_mm3"].get<double>() - (8000 - wedge)) < 0.01);
    }

    void mirrorAndPatternThroughTools() {
        initialize();
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {10, 10}}}}}})
                     .first);
        QVERIFY(!tool("extrude", {{"sketch", 1}, {"distance", 5}}).first);
        const auto [e1, m] = tool("mirror", {{"bodies", {"Body1"}}, {"plane", "YZ"}, {"join", false}});
        QVERIFY2(!e1, m.dump().c_str());
        QCOMPARE(int(m["bodies"].size()), 2);
        const auto [e2, p] = tool("pattern", {{"type", "rectangular"}, {"bodies", {"Body1"}}, {"direction", "y"},
                                              {"count", 4}, {"spacing", 20}, {"join", false}});
        QVERIFY2(!e2, p.dump().c_str());
        QCOMPARE(int(p["bodies"].size()), 5);
        // Features: a hole in a plate, around a hole's own axis is pointless; around z.
        QVERIFY(tool("pattern", {{"type", "circular"}, {"features", {1}}}).first); // a sketch cannot be repeated
        QVERIFY(tool("pattern", {{"type", "rectangular"}, {"bodies", {"Body1"}}}).first); // no spacing
    }

    void threadsAndTappedHoles() {
        initialize();
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {30, 20}}}}}})
                     .first);
        QVERIFY(!tool("extrude", {{"sketch", 1}, {"distance", 10}}).first);
        const json top = tool("list_faces", {{"body", "Body1"}, {"normal", "+z"}}).second;
        const int topIndex = top["faces"][0]["index"];
        auto [e1, h1] = tool("hole", {{"face", {{"body", "Body1"}, {"index", topIndex}}}, {"points", {{8, 10, 10}}},
                                      {"type", "tapped"}, {"thread", "M6"}, {"through_all", true}});
        QVERIFY2(!e1, h1.dump().c_str());
        const double v1 = h1["bodies"][0]["volume_mm3"];
        QVERIFY(v1 < 6000 - M_PI * 2.5 * 2.5 * 10); // more than the tap drill came out
        // A plain 5 mm hole, then threaded by the thread tool (its size found from the diameter).
        const json top2 = tool("list_faces", {{"body", "Body1"}, {"normal", "+z"}}).second;
        QVERIFY(!tool("hole", {{"face", {{"body", "Body1"}, {"index", top2["faces"][0]["index"]}}},
                               {"points", {{22, 10, 10}}}, {"diameter", 5}, {"through_all", true}})
                     .first);
        const json cyl = tool("list_faces", {{"body", "Body1"}, {"type", "cylinder"}, {"near", {22, 10, 5}}}).second;
        const auto [e2, t] = tool("thread", {{"faces", {{{"body", "Body1"}, {"index", cyl["faces"][0]["index"]}}}}});
        QVERIFY2(!e2, t.dump().c_str());
        QVERIFY(tool("thread", {{"faces", {{{"body", "Body1"}, {"index", cyl["faces"][0]["index"]}}}}, {"size", "M99"}}).first);
        QVERIFY(tool("hole", {{"face", {{"body", "Body1"}, {"index", topIndex}}}, {"points", {{15, 3, 10}}},
                              {"type", "tapped"}})
                     .first); // no thread size
    }

    // Text: as sketch regions to extrude, and engraved / embossed on faces.
    void textInSketchesAndOnFaces() {
        initialize();
        // A sketch with a plate and a name in it: the letters are regions of their own.
        const auto [e0, sk] = tool("create_sketch", {{"plane", "XY"},
                                                     {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {60, 20}}},
                                                                   {{"type", "text"}, {"at", {5, 5}}, {"text", "HI"}, {"size", 10}}}}});
        QVERIFY2(!e0, sk.dump().c_str());
        QCOMPARE(sk["entities"][1]["type"].get<std::string>(), std::string("text"));
        QCOMPARE(sk["entities"][1]["letter_regions"].get<int>(), 2);
        QCOMPARE(int(sk["profiles"].size()), 3); // the plate with the letters cut out, H and I
        QVERIFY(tool("create_sketch", {{"plane", "XY"}, {"entities", {{{"type", "text"}, {"at", {0, 0}}, {"text", " "}}}}}).first);

        // A box: text engraved into its top, raised on its front.
        const json boxSketch = tool("create_sketch", {{"plane", "XY"},
                                                      {"entities", {{{"type", "rectangle"}, {"corner1", {100, 0}}, {"corner2", {160, 30}}}}}})
                                   .second;
        const auto [e1, ex] = tool("extrude", {{"sketch", boxSketch["sketch"]}, {"distance", 10}});
        QVERIFY2(!e1, ex.dump().c_str());
        const std::string body = ex["bodies"][0]["id"];
        const json top = tool("list_faces", {{"body", body}, {"normal", "+z"}}).second;
        const auto [e2, t1] = tool("emboss_text", {{"face", {{"body", body}, {"index", top["faces"][0]["index"]}}},
                                                   {"text", "CAD"}, {"size", 8}, {"depth", 1}, {"at", {130, 15, 10}}});
        QVERIFY2(!e2, t1.dump().c_str());
        QVERIFY(t1["volume_change_mm3"].get<double>() < -10);
        QCOMPARE(t1["position"][0].get<double>(), 130.0);
        QCOMPARE(t1["feature"]["type"].get<std::string>(), std::string("text"));
        const json front = tool("list_faces", {{"body", body}, {"normal", "-y"}}).second;
        const auto [e3, t2] = tool("emboss_text", {{"face", {{"body", body}, {"index", front["faces"][0]["index"]}}},
                                                   {"text", "UP"}, {"direction", "emboss"}, {"depth", 0.8}, {"mirror", true}});
        QVERIFY2(!e3, t2.dump().c_str());
        QVERIFY(t2["volume_change_mm3"].get<double>() > 5);
        // In the middle of the front face (x 130, z 5).
        QVERIFY(std::fabs(t2["text_middle"][0].get<double>() - 130) < 1e-3 && std::fabs(t2["text_middle"][2].get<double>() - 5) < 1e-3);
        QVERIFY(tool("emboss_text", {{"face", {{"body", body}, {"index", 1}}}, {"text", "X"}, {"direction", "sideways"}}).first);

        // Round a cylinder.
        const json circle =
            tool("create_sketch", {{"plane", "XY"}, {"entities", {{{"type", "circle"}, {"center", {0, 60}}, {"radius", 12}}}}}).second;
        const auto [e4, cyl] = tool("extrude", {{"sketch", circle["sketch"]}, {"distance", 30}});
        QVERIFY2(!e4, cyl.dump().c_str());
        std::string cylBody;
        for(const auto &b : cyl["bodies"])
            if(b["id"] != body) cylBody = b["id"];
        const json side = tool("list_faces", {{"body", cylBody}, {"type", "cylinder"}}).second;
        const auto [e5, t3] = tool("emboss_text", {{"face", {{"body", cylBody}, {"index", side["faces"][0]["index"]}}},
                                                   {"text", "ROUND"}, {"size", 6}});
        QVERIFY2(!e5, t3.dump().c_str());
        QVERIFY(t3["volume_change_mm3"].get<double>() < -5);
        QVERIFY(t3["face_frame"].get<std::string>().rfind("curved", 0) == 0);
    }

    // Print materials per body: set, kept where not given, reported, undone.
    void materialsPerBody() {
        initialize();
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {20, 10}}},
                                                       {{"type", "circle"}, {"center", {40, 5}}, {"radius", 5}}}}})
                     .first);
        const auto [e0, ex] = tool("extrude", {{"sketch", 1}, {"distance", 5}});
        QVERIFY2(!e0, ex.dump().c_str());
        QCOMPARE(int(ex["bodies"].size()), 2);
        const std::string a = ex["bodies"][0]["id"], b = ex["bodies"][1]["id"];
        QCOMPARE(ex["bodies"][0]["material"]["material"].get<std::string>(), std::string("PLA"));
        const auto [e1, r1] = tool("set_material", {{"bodies", {a}}, {"material", "PETG"}, {"finish", "semitransparent"},
                                                   {"color", "Ice Blue"}});
        QVERIFY2(!e1, r1.dump().c_str());
        QCOMPARE(m_window->document().bodyMaterial(a).finish, cad::Finish::Translucent);
        QCOMPARE(QString::fromStdString(m_window->document().bodyMaterial(a).colorName), QStringLiteral("Ice Blue"));
        QCOMPARE(m_window->document().bodyMaterial(b), cad::defaultBodyMaterial());
        // Only the finish: silk; the colour switches to one that comes in silk.
        const auto [e2, r2] = tool("set_material", {{"finish", "silk"}});
        QVERIFY2(!e2, r2.dump().c_str());
        QCOMPARE(m_window->document().bodyMaterial(a).material, cad::PrintMaterial::PETG);
        QCOMPARE(m_window->document().bodyMaterial(b).finish, cad::Finish::Silk);
        const cad::FilamentColor *c = cad::findFilamentColor(m_window->document().bodyMaterial(a).colorName);
        QVERIFY(c && (c->finishes & (1u << int(cad::Finish::Silk))));
        QVERIFY(tool("set_material", {{"color", "Plaid"}}).first);
        QVERIFY(tool("set_material", {{"material", "ABS"}}).first);
        QVERIFY(tool("set_material", {{"bodies", {a}}}).first);
        QVERIFY(!tool("set_material", {{"bodies", {b}}, {"color", "#ff8000"}}).first);
        QCOMPARE(m_window->document().bodyMaterial(b).rgb, 0xff8000u);
        QVERIFY(!tool("set_material", {{"reset", true}}).first);
        QCOMPARE(m_window->document().bodyMaterial(a), cad::defaultBodyMaterial());
        // Several bodies changed at once: one undo step.
        QVERIFY(!tool("set_material", {{"color", "Signal Red"}}).first);
        QCOMPARE(QString::fromStdString(m_window->document().undoLabel()), QStringLiteral("Body Material"));
        QVERIFY(!tool("undo", json::object()).first);
        QCOMPARE(m_window->document().bodyMaterial(a), cad::defaultBodyMaterial());
        QCOMPARE(m_window->document().bodyMaterial(b), cad::defaultBodyMaterial());
    }

    void sketchMirrorPatternProjectAndConstruction() {
        initialize();
        // An open U onto the Y axis, then mirrored about it: one closed profile.
        const auto [e0, u] = tool("create_sketch", {{"plane", "XY"},
                                                    {"entities", {{{"type", "polyline"},
                                                                   {"points", {{0, 0}, {20, 0}, {20, 10}, {0, 10}}}}}}});
        QVERIFY2(!e0, u.dump().c_str());
        const int sketch = u["sketch"];
        std::vector<int> lines;
        for(const json &c : u["curves"])
            if(c["type"] == "line") lines.push_back(c["id"]);
        QCOMPARE(int(lines.size()), 3);
        QCOMPARE(int(u["profiles"].size()), 0);
        const auto [e1, m] = tool("sketch_mirror", {{"sketch", sketch}, {"entities", lines}, {"line", "y_axis"}});
        QVERIFY2(!e1, m.dump().c_str());
        QCOMPARE(int(m["profiles"].size()), 1);
        QCOMPARE(m["profiles"][0]["area_mm2"].get<double>(), 400.0);
        QVERIFY(tool("sketch_mirror", {{"sketch", sketch}, {"entities", lines}, {"line", "z_axis"}}).first);
        // A circle copied around the origin: 6 in all.
        const auto [e2, c] = tool("add_to_sketch", {{"sketch", sketch},
                                                    {"entities", {{{"type", "circle"}, {"center", {0, 30}}, {"radius", 3}}}}});
        QVERIFY2(!e2, c.dump().c_str());
        const int circle = c["entities"][0]["id"];
        const auto [e3, p] = tool("sketch_pattern", {{"sketch", sketch}, {"entities", {circle}}, {"center", {0, 0}}, {"count", 6}});
        QVERIFY2(!e3, p.dump().c_str());
        int circles = 0;
        for(const json &cv : p["curves"]) circles += cv["type"] == "circle";
        QCOMPARE(circles, 6);
        QVERIFY(tool("sketch_pattern", {{"sketch", sketch}, {"entities", {circle}}, {"center", {0, 0}}, {"count", 1}}).first);
        // Construction: the mirrored outline stops being a profile.
        const auto [e4, k] = tool("set_construction", {{"sketch", sketch}, {"entities", lines}});
        QVERIFY2(!e4, k.dump().c_str());
        QCOMPARE(int(k["profiles"].size()), 6); // just the circles
        QVERIFY(!tool("set_construction", {{"sketch", sketch}, {"entities", lines}, {"construction", false}}).first);
        // A sketch on XZ projecting all of it that can be (lines, not circles: a tilted plane).
        const auto [e5, side] = tool("create_sketch", {{"plane", "XZ"},
                                                       {"entities", {{{"type", "line"}, {"from", {-50, 40}}, {"to", {50, 40}}}}}});
        QVERIFY2(!e5, side.dump().c_str());
        const auto [e6, pr] = tool("project_to_sketch", {{"sketch", side["sketch"]}, {"from_sketch", sketch}});
        QVERIFY2(!e6, pr.dump().c_str());
        int projected = 0;
        for(const json &cv : pr["curves"]) projected += cv.contains("projected_from");
        QCOMPARE(projected, 6);
        // Projecting from a later sketch is refused.
        QVERIFY(tool("project_to_sketch", {{"sketch", sketch}, {"from_sketch", side["sketch"]}}).first);
    }

    void renderSettingsAndAPicture() {
        initialize();
        QVERIFY(!tool("create_sketch", {{"plane", "XY"},
                                         {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {20, 10}}}}}})
                     .first);
        QVERIFY(!tool("extrude", {{"sketch", 1}, {"distance", 8}}).first);
        const auto [e0, r0] = tool("set_render", {{"plate", "smooth_pei"}, {"lighting", "daylight"}, {"layer_height", 0.12},
                                                  {"ray_traced", false}});
        QVERIFY2(!e0, r0.dump().c_str());
        QCOMPARE(r0["plate"].get<std::string>(), std::string("smooth_pei"));
        const cad::RenderSettings &s = m_window->document().renderSettings();
        QCOMPARE(s.plate, cad::BuildPlateKind::SmoothPEI);
        QCOMPARE(s.lighting, cad::Lighting::Daylight);
        QCOMPARE(s.layerHeight, 0.12);
        QVERIFY(!s.rayTraced);
        QCOMPARE(m_window->viewport()->displayStyle(), DisplayStyle::Rendered);
        QVERIFY(tool("set_render", {{"plate", "glass"}}).first);
        QVERIFY(tool("set_render", {{"layer_height", "thin"}}).first);
        QCOMPARE(tool("get_design", json::object()).second["render"]["lighting"].get<std::string>(), std::string("daylight"));
        // A path-traced picture of the view.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("r.png"));
        const auto [e1, r1] = tool("render_image", {{"path", path.toStdString()}, {"width", 96}, {"height", 64}, {"samples", 4}});
        QVERIFY2(!e1, r1.dump().c_str());
        const QImage img(path);
        QCOMPARE(img.size(), QSize(96, 64));
        QCOMPARE(r1["samples"].get<int>(), 4);
        // The part (its own colour) is in the middle, the plate below.
        QVERIFY(img.pixelColor(48, 32) != img.pixelColor(2, 2));
        QVERIFY(tool("render_image", {{"width", 4}}).first);
    }

    void aBatchBuildsAPartInOneCall() {
        initialize();
        const json calls = {
            {{"tool", "create_sketch"},
             {"arguments", {{"plane", "XY"}, {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {30, 20}}}}}}}},
            {{"tool", "extrude"}, {"arguments", {{"sketch", 1}, {"distance", 10}}}},
            {{"tool", "get_design"}}};
        const json r = rpc("tools/call", {{"name", "batch"}, {"arguments", {{"calls", calls}}}})["result"];
        QVERIFY2(!r.value("isError", false), r.dump().c_str());
        const json &content = r["content"];
        QCOMPARE(int(content.size()), 3);
        QVERIFY(content[1]["text"].get<std::string>().rfind("[2/3] extrude: ", 0) == 0);
        QCOMPARE(int(m_window->document().features().size()), 2);
        // A failing call stops the batch; the rest are skipped.
        const json bad = {{{"tool", "fillet"}, {"arguments", {{"edges", {{{"body", "Body1"}, {"index", 999}}}}, {"radius", 1}}}},
                          {{"tool", "extrude"}, {"arguments", {{"sketch", 1}, {"distance", 3}}}}};
        const json r2 = rpc("tools/call", {{"name", "batch"}, {"arguments", {{"calls", bad}}}})["result"];
        QVERIFY(r2.value("isError", false));
        QVERIFY(r2["content"][1]["text"].get<std::string>().find("skipped") != std::string::npos);
        QCOMPARE(int(m_window->document().features().size()), 2);
        // No batches inside batches.
        const json nested = {{{"tool", "batch"}, {"arguments", {{"calls", calls}}}}};
        const json r3 = rpc("tools/call", {{"name", "batch"}, {"arguments", {{"calls", nested}}}})["result"];
        QVERIFY(r3.value("isError", false));
        QCOMPARE(int(m_window->document().features().size()), 2);
    }

    void aPartBuiltThroughTools() {
        initialize();
        // A 60 x 40 plate with a 10 mm hole in its sketch.
        const auto sketchCall = tool("create_sketch", {{"plane", "XY"},
                                                     {"entities", {{{"type", "rectangle"}, {"corner1", {0, 0}}, {"corner2", {60, 40}}},
                                                                   {{"type", "circle"}, {"center", {30, 20}}, {"diameter", 10}}}}});
        const bool err1 = sketchCall.first;
        const json sketch = sketchCall.second;
        QVERIFY2(!err1, sketch.dump().c_str());
        QCOMPARE(int(sketch["profiles"].size()), 2);
        QCOMPARE(int(sketch["parameters"].size()), 3);
        QCOMPARE(sketch["degrees_of_freedom"].get<int>(), 0); // dimensioned and anchored
        const int sid = sketch["sketch"];
        // Only the plate region (not the hole's disk).
        const auto exCall = tool("extrude", {{"sketch", sid}, {"profile_points", {{5, 5}}}, {"distance", 8}});
        const bool err2 = exCall.first;
        const json ex = exCall.second;
        QVERIFY2(!err2, ex.dump().c_str());
        const double plate = 60 * 40 * 8 - M_PI * 25 * 8;
        QVERIFY(std::fabs(ex["bodies"][0]["volume_mm3"].get<double>() - plate) < 0.01);
        const std::string body = ex["bodies"][0]["id"];
        // Round the four vertical edges.
        const auto edgesCall = tool("list_edges", {{"body", body}, {"direction", "z"}});
        const bool err3 = edgesCall.first;
        const json edges = edgesCall.second;
        QVERIFY(!err3);
        json picks = json::array();
        for(const auto &e : edges["edges"])
            if(e["length"].get<double>() > 7.9) {
                const double x = e["midpoint"][0], y = e["midpoint"][1];
                const bool corner = (x < 0.01 || x > 59.99) && (y < 0.01 || y > 39.99); // not the hole's seam
                if(corner) picks.push_back({{"body", body}, {"index", e["index"]}});
            }
        QCOMPARE(int(picks.size()), 4);
        const auto filCall = tool("fillet", {{"edges", picks}, {"radius", 4}});
        const bool err4 = filCall.first;
        const json fil = filCall.second;
        QVERIFY2(!err4, fil.dump().c_str());
        // Two counterbored holes into the top face.
        const auto facesCall = tool("list_faces", {{"body", body}, {"normal", "+z"}});
        const bool err5 = facesCall.first;
        const json faces = facesCall.second;
        QVERIFY(!err5 && faces["faces"].size() == 1);
        const auto holeCall = tool("hole", {{"face", {{"body", body}, {"index", faces["faces"][0]["index"]}}},
                                          {"points", {{10, 20, 8}, {50, 20, 8}}},
                                          {"type", "counterbore"},
                                          {"through_all", true}});
        const bool err6 = holeCall.first;
        const json hole = holeCall.second;
        QVERIFY2(!err6, hole.dump().c_str());
        // A bad edge comes back as an error the agent can read; nothing is added.
        const size_t features = m_window->document().features().size();
        const auto badCall = tool("fillet", {{"edges", {{{"body", body}, {"index", 9999}}}}, {"radius", 1}});
        const bool err7 = badCall.first;
        const json bad = badCall.second;
        QVERIFY(err7);
        QVERIFY(bad.get<std::string>().find("not in 1..") != std::string::npos);
        const auto bigCall = tool("fillet", {{"edges", picks.size() ? json::array({picks[0]}) : json::array()}, {"radius", 80}});
        const bool err8 = bigCall.first;
        const json big = bigCall.second;
        Q_UNUSED(big);
        QVERIFY(err8);
        QCOMPARE(m_window->document().features().size(), features);
        // Parameters: the width grows from the anchored corner.
        const auto designCall = tool("get_design");
        const bool err9 = designCall.first;
        const json design = designCall.second;
        QVERIFY(!err9);
        const std::string width = design["features"][0]["parameters"][0]["name"];
        const auto changedCall = tool("set_parameter", {{"name", width}, {"expression", 80}});
        const bool err10 = changedCall.first;
        const json changed = changedCall.second;
        QVERIFY2(!err10, changed.dump().c_str());
        QCOMPARE(changed["bodies"][0]["bounding_box"]["min"][0].get<double>(), 0.0);
        QCOMPARE(changed["bodies"][0]["bounding_box"]["max"][0].get<double>(), 80.0);
        // Print it: the STL passes the check; the STEP reads back.
        const auto stlCall = tool("export_stl", {{"path", m_dir->filePath(QStringLiteral("part.stl")).toStdString()}});
        const bool err11 = stlCall.first;
        const json stl = stlCall.second;
        QVERIFY2(!err11, stl.dump().c_str());
        QVERIFY(stl["report"]["printable"].get<bool>() && stl["report"]["shells"] == 1);
        QVERIFY(QFile::exists(m_dir->filePath(QStringLiteral("part.stl"))));
        const auto stepCall = tool("export_step", {{"path", m_dir->filePath(QStringLiteral("part.step")).toStdString()}});
        const bool err12 = stepCall.first;
        const json step = stepCall.second;
        QVERIFY(!err12);
        QVERIFY(std::fabs(step["read_back_volume_mm3"].get<double>() - step["model_volume_mm3"].get<double>()) < 0.01);
        // Undo goes back through the agent's steps; the timeline shows them.
        const auto undoneCall = tool("undo");
        const bool err13 = undoneCall.first;
        const json undone = undoneCall.second;
        QVERIFY(!err13);
        const auto afterCall = tool("get_design");
        const bool err14 = afterCall.first;
        const json after = afterCall.second;
        QCOMPARE(after["bodies"][0]["bounding_box"]["max"][0].get<double>(), 60.0);
        int calls = 0;
        for(const McpEvent &e : m_window->mcpLog()->events()) calls += e.kind == McpEvent::Kind::Call;
        QVERIFY(calls >= 12);
    }

    void screenshotsArePngs() {
        initialize();
        tool("create_sketch", {{"plane", "XY"}, {"entities", {{{"type", "circle"}, {"center", {0, 0}}, {"radius", 10}}}}});
        tool("extrude", {{"sketch", 1}, {"distance", 20}});
        const json r = rpc("tools/call", {{"name", "screenshot"}, {"arguments", {{"view", "home"}, {"max_width", 640}}}})["result"];
        QCOMPARE(QString::fromStdString(r["content"][0]["type"]), QStringLiteral("image"));
        const QByteArray png = QByteArray::fromBase64(QByteArray::fromStdString(r["content"][0]["data"]));
        const QImage img = QImage::fromData(png, "PNG");
        QVERIFY(!img.isNull());
        QVERIFY(img.width() <= 640);
    }

    void turningItOffStopsIt() {
        McpSettings s = m_window->mcpServer()->settings();
        s.enabled = false;
        m_window->mcpServer()->apply(s);
        QCOMPARE(m_window->mcpServer()->state(), McpServer::State::Off);
        QCOMPARE(m_window->mcpButton()->state(), McpServer::State::Off);
        QCOMPARE(post(R"({"jsonrpc":"2.0","id":1,"method":"ping"})").status, 0); // connection refused
    }

    void theLogFileRollsAtOneMegabyte() {
        McpLog log;
        log.setDirectory(m_dir->filePath(QStringLiteral("roll")));
        const QString detail(1000, QLatin1Char('x'));
        for(int i = 0; i < 2500; ++i) log.add(McpEvent::Kind::Call, QStringLiteral("agent"), QStringLiteral("call %1").arg(i), detail);
        QFile f(log.filePath());
        QVERIFY(f.size() <= McpLog::maxBytes());
        QVERIFY(f.size() > McpLog::maxBytes() / 2);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QList<QByteArray> lines = f.readAll().trimmed().split('\n');
        QVERIFY(lines.last().contains("call 2499"));
        QVERIFY(lines.first().startsWith("{")); // cut at a line boundary
        // A new log on the same folder shows the newest events.
        McpLog again;
        again.setDirectory(m_dir->filePath(QStringLiteral("roll")));
        QVERIFY(!again.events().empty());
        QCOMPARE(again.events().back().summary, QStringLiteral("call 2499"));
    }

    void theDialogShowsTheLogAndMakesTokens() {
        McpDialog *dlg = m_window->openMcpDialog();
        QVERIFY(dlg);
        QVERIFY(dlg->enabledBox()->isChecked());
        const int rows = dlg->logView()->topLevelItemCount();
        initialize();
        QTRY_VERIFY(dlg->logView()->topLevelItemCount() > rows);
        QVERIFY(dlg->statusLabel()->text().contains(QStringLiteral("Connected")));
        const QString before = dlg->tokenEdit()->text();
        dlg->generateButton()->click();
        const QString token = dlg->tokenEdit()->text();
        QCOMPARE(token.size(), 43);
        QVERIFY(token != before);
        QVERIFY(dlg->snippet().contains(token));
        // Hermes: the plugin folder (not the repository) and `hermes mcp add` with this port.
        dlg->clientBox()->setCurrentIndex(dlg->clientBox()->findText(QStringLiteral("Hermes Agent")));
        QVERIFY(dlg->snippet().contains(QStringLiteral("hermes plugins install RidgebackZulu/cadjitsu/plugins/cadjitsu")));
        QVERIFY(dlg->snippet().contains(
            QStringLiteral("hermes mcp add cadjitsu --url http://127.0.0.1:%1/mcp").arg(dlg->portBox()->value())));
        QVERIFY(dlg->snippet().contains(token));
        dlg->clientBox()->setCurrentIndex(0);
        dlg->applyButton()->click();
        QCOMPARE(m_window->mcpServer()->settings().token, token);
        QCOMPARE(McpSettings::load().token, token);
        // The old token no longer works; the new one does.
        QCOMPARE(post(R"({"jsonrpc":"2.0","id":1,"method":"ping"})").status, 401);
        QCOMPARE(post(R"({"jsonrpc":"2.0","id":1,"method":"ping"})", token).status, 200);
        dlg->close();
    }

    // The skill's tool reference (plugins/cadjitsu/skills/cadjitsu-cad/references/tools.md)
    // is generated from the tool list; it must name every tool and argument.
    void theSkillDocumentsEveryTool() {
        QFile f(QStringLiteral(CADJITSU_SOURCE_DIR "/plugins/cadjitsu/skills/cadjitsu-cad/references/tools.md"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString doc = QString::fromUtf8(f.readAll());
        QFile skill(QStringLiteral(CADJITSU_SOURCE_DIR "/plugins/cadjitsu/skills/cadjitsu-cad/SKILL.md"));
        QVERIFY(skill.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(skill.readAll()).startsWith(QStringLiteral("---\nname: cadjitsu-cad\n")));
        initialize();
        const json tools = rpc("tools/list")["result"]["tools"];
        for(const auto &t : tools) {
            const QString name = QString::fromStdString(t["name"]);
            QVERIFY2(doc.contains(QStringLiteral("## `%1`").arg(name)), qPrintable(name));
            for(const auto &item : t["inputSchema"]["properties"].items())
                QVERIFY2(doc.contains(QStringLiteral("`%1`").arg(QString::fromStdString(item.key()))), qPrintable(name));
        }
    }
};

CADJITSU_REGISTER_TEST(McpTests)

#include "tst_mcp.moc"
