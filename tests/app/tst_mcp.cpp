// The MCP server: authentication and origin checks, the connection button,
// a whole part built through tools (sketch, extrude, fillet, hole, parameter
// change, export), errors reported to the agent, screenshots, the rolling
// event log file and the dialog.
#include "TestRegistry.h"

#include "MainWindow.h"
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

using namespace cadly;
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
                                          {"clientInfo", {{"name", "cadly-test"}, {"version", "1.0"}}},
                                          {"capabilities", json::object()}});
        QCOMPARE(QString::fromStdString(r["result"]["serverInfo"]["name"]), QStringLiteral("cadly"));
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
        QCOMPARE(m_window->mcpServer()->clientName(), QStringLiteral("cadly-test 1.0"));
        QTRY_VERIFY(m_window->mcpButton()->glow() > 0.05); // it pulses
        const json tools = rpc("tools/list")["result"]["tools"];
        std::set<std::string> names;
        for(const auto &t : tools) {
            names.insert(t["name"]);
            QVERIFY(t.contains("inputSchema") && t.contains("description"));
        }
        for(const char *n : {"get_design", "create_sketch", "extrude", "fillet", "chamfer", "hole", "combine", "list_edges",
                             "list_faces", "set_parameter", "edit_feature", "undo", "screenshot", "export_stl", "export_step"})
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
        QCOMPARE(m_window->mcpServer()->clientName(), QStringLiteral("cadly-test 1.0")); // remembered
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
        QVERIFY(dlg->snippet().contains(QStringLiteral("hermes plugins install RidgebackZulu/cadly/plugins/cadly")));
        QVERIFY(dlg->snippet().contains(
            QStringLiteral("hermes mcp add cadly --url http://127.0.0.1:%1/mcp").arg(dlg->portBox()->value())));
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

    // The skill's tool reference (plugins/cadly/skills/cadly-cad/references/tools.md)
    // is generated from the tool list; it must name every tool and argument.
    void theSkillDocumentsEveryTool() {
        QFile f(QStringLiteral(CADLY_SOURCE_DIR "/plugins/cadly/skills/cadly-cad/references/tools.md"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString doc = QString::fromUtf8(f.readAll());
        QFile skill(QStringLiteral(CADLY_SOURCE_DIR "/plugins/cadly/skills/cadly-cad/SKILL.md"));
        QVERIFY(skill.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(skill.readAll()).startsWith(QStringLiteral("---\nname: cadly-cad\n")));
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

CADLY_REGISTER_TEST(McpTests)

#include "tst_mcp.moc"
