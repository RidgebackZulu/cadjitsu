#include "mcp/McpServer.h"

#include "base/Version.h"
#include "mcp/McpLog.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <QUuid>

using json = nlohmann::json;

namespace cadjitsu {

namespace {

constexpr qint64 kMaxBody = 16 * 1024 * 1024;
constexpr int kConnectedSeconds = 300;
const char *const kProtocols[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

// Constant time, so the token cannot be guessed a character at a time.
bool sameToken(const QByteArray &a, const QByteArray &b) {
    if(a.size() != b.size() || a.isEmpty()) return false;
    unsigned char diff = 0;
    for(qsizetype i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

bool localOrigin(const QString &origin) {
    if(origin.isEmpty() || origin == QLatin1String("null")) return true;
    for(const char *h : {"http://localhost", "http://127.0.0.1", "https://localhost", "https://127.0.0.1", "http://[::1]"})
        if(origin == QLatin1String(h) || origin.startsWith(QLatin1String(h) + QLatin1Char(':'))) return true;
    return false;
}

QString shorten(const std::string &s, int n = 300) {
    QString q = QString::fromStdString(s);
    return q.size() > n ? q.left(n) + QStringLiteral("…") : q;
}

json rpcError(const json &id, int code, const std::string &message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

} // namespace

// --- settings --------------------------------------------------------------------

McpSettings McpSettings::load() {
    QSettings s;
    McpSettings m;
    m.enabled = s.value(QStringLiteral("mcp/enabled"), false).toBool();
    m.port = s.value(QStringLiteral("mcp/port"), 7823).toInt();
    m.token = s.value(QStringLiteral("mcp/token")).toString();
    if(m.token.isEmpty()) m.token = generateToken();
    return m;
}

void McpSettings::save() const {
    QSettings s;
    s.setValue(QStringLiteral("mcp/enabled"), enabled);
    s.setValue(QStringLiteral("mcp/port"), port);
    s.setValue(QStringLiteral("mcp/token"), token);
}

QString McpSettings::generateToken() {
    QByteArray bytes(32, Qt::Uninitialized);
    QRandomGenerator::system()->generate(bytes.begin(), bytes.end());
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

// --- server ------------------------------------------------------------------------

McpServer::McpServer(McpLog *log, McpToolProvider *tools, QObject *parent)
    : QObject(parent), m_log(log), m_tools(tools) {
    m_idle = new QTimer(this);
    m_idle->setInterval(5000);
    connect(m_idle, &QTimer::timeout, this, &McpServer::updateState);
}

McpServer::~McpServer() {
    if(m_server) m_server->close();
}

bool McpServer::listening() const { return m_server && m_server->isListening(); }

quint16 McpServer::port() const { return listening() ? m_server->serverPort() : 0; }

bool McpServer::apply(const McpSettings &s) {
    const bool restart = !listening() || s.port != m_settings.port || s.token != m_settings.token;
    const bool wasOn = listening();
    m_settings = s;
    m_error.clear();
    if(!s.enabled) {
        if(wasOn) stop();
        return true;
    }
    if(!restart) return true;
    if(wasOn) stop();
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &McpServer::onConnection);
    if(!m_server->listen(QHostAddress::LocalHost, quint16(s.port))) {
        m_error = m_server->errorString();
        m_log->add(McpEvent::Kind::Error, {}, tr("Could not start on port %1: %2").arg(s.port).arg(m_error));
        delete m_server;
        m_server = nullptr;
        updateState();
        return false;
    }
    m_log->add(McpEvent::Kind::Server, {}, tr("Listening on http://127.0.0.1:%1/mcp").arg(m_server->serverPort()));
    m_idle->start();
    updateState();
    return true;
}

void McpServer::stop() {
    if(m_server) {
        m_server->close();
        // Disconnecting erases from m_buffers (the disconnected handler): take them out first.
        std::vector<QTcpSocket *> open;
        for(auto &[socket, buf] : m_buffers) open.push_back(socket);
        m_buffers.clear();
        for(QTcpSocket *socket : open) {
            socket->disconnect(this);
            socket->abort();
            socket->deleteLater();
        }
        m_queue.clear();
        m_server->deleteLater();
        m_server = nullptr;
        m_log->add(McpEvent::Kind::Server, {}, tr("Server stopped"));
    }
    m_sessions.clear();
    m_idle->stop();
    updateState();
}

QString McpServer::clientOf(const QString &sessionId) const {
    auto it = m_sessions.find(sessionId);
    return it == m_sessions.end() ? QString() : it->second.client;
}

QString McpServer::clientName() const { return clientOf(m_lastSession); }

void McpServer::updateState() {
    State s = State::Off;
    if(listening()) {
        s = State::Listening;
        const QDateTime now = QDateTime::currentDateTime();
        for(const auto &[id, session] : m_sessions)
            if(session.lastSeen.secsTo(now) < kConnectedSeconds) s = State::Connected;
    }
    if(s != m_state) {
        m_state = s;
        emit stateChanged(s);
    }
}

void McpServer::onConnection() {
    while(m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        m_buffers[socket];
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.erase(socket);
            socket->deleteLater();
        });
    }
}

void McpServer::onReadyRead(QTcpSocket *socket) {
    auto it = m_buffers.find(socket);
    if(it == m_buffers.end()) return;
    QByteArray &buf = it->second;
    buf += socket->readAll();
    // Requests on this connection, one after the other (keep-alive).
    for(;;) {
        const qsizetype end = buf.indexOf("\r\n\r\n");
        if(end < 0) {
            if(buf.size() > 64 * 1024) respond(socket, 431, R"({"error":"headers too large"})");
            return;
        }
        Request r;
        const QList<QByteArray> lines = buf.left(end).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        r.method = QString::fromLatin1(first.value(0));
        r.path = QString::fromLatin1(first.value(1));
        for(qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray l = lines[i].trimmed();
            const qsizetype c = l.indexOf(':');
            if(c > 0) r.headers[QString::fromLatin1(l.left(c).trimmed()).toLower()] = QString::fromLatin1(l.mid(c + 1).trimmed());
        }
        const qint64 length = r.headers.count(QStringLiteral("content-length"))
                                  ? r.headers[QStringLiteral("content-length")].toLongLong()
                                  : 0;
        if(length < 0 || length > kMaxBody) {
            respond(socket, 413, R"({"error":"request too large"})");
            buf.clear();
            return;
        }
        if(buf.size() < end + 4 + length) return; // the rest of the body is still coming
        r.body = buf.mid(end + 4, length);
        buf.remove(0, end + 4 + length);
        m_queue.emplace_back(socket, std::move(r));
        processQueue();
        if(!m_buffers.count(socket)) return;
    }
}

// One request at a time: a tool may spin the event loop while the model
// recomputes, and must not be re-entered by the next request.
void McpServer::processQueue() {
    if(m_busy) return;
    m_busy = true;
    while(!m_queue.empty()) {
        auto [socket, r] = std::move(m_queue.front());
        m_queue.pop_front();
        if(socket) handle(socket, r);
    }
    m_busy = false;
}

void McpServer::handle(QTcpSocket *socket, const Request &r) {
    auto header = [&](const char *name) {
        auto it = r.headers.find(QString::fromLatin1(name));
        return it == r.headers.end() ? QString() : it->second;
    };
    if(!localOrigin(header("origin"))) {
        m_log->add(McpEvent::Kind::Auth, {}, tr("Refused a request from %1").arg(header("origin")));
        respond(socket, 403, R"({"error":"origin not allowed"})");
        return;
    }
    const QString path = r.path.section(QLatin1Char('?'), 0, 0);
    if(path != QLatin1String("/mcp") && path != QLatin1String("/mcp/")) {
        respond(socket, 404, R"({"error":"not found; the MCP endpoint is /mcp"})");
        return;
    }
    QString auth = header("authorization");
    const QByteArray given = auth.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive) ? auth.mid(7).trimmed().toLatin1()
                                                                                             : QByteArray();
    if(!sameToken(given, m_settings.token.toLatin1())) {
        m_log->add(McpEvent::Kind::Auth, {}, given.isEmpty() ? tr("Refused a request without a token")
                                                             : tr("Refused a request with a wrong token"));
        respond(socket, 401, R"({"error":"missing or wrong bearer token"})", "application/json",
                {{"WWW-Authenticate", "Bearer realm=\"cadjitsu\""}});
        return;
    }
    QString sessionId = header("mcp-session-id");
    if(r.method == QLatin1String("DELETE")) {
        if(m_sessions.count(sessionId)) {
            m_log->add(McpEvent::Kind::Disconnect, clientOf(sessionId), tr("Session ended"));
            m_sessions.erase(sessionId);
        }
        respond(socket, 200, "{}");
        updateState();
        return;
    }
    if(r.method != QLatin1String("POST")) {
        respond(socket, 405, R"j({"error":"use POST (this server does not stream)"})j", "application/json", {{"Allow", "POST, DELETE"}});
        return;
    }
    ++m_requests;
    json msg = json::parse(r.body.constData(), r.body.constData() + r.body.size(), nullptr, false);
    if(msg.is_discarded()) {
        respond(socket, 400, QByteArray::fromStdString(rpcError(nullptr, -32700, "parse error").dump()));
        return;
    }
    // Any authenticated request is activity. A session id this server does not know
    // (from before a restart: clients keep using theirs) or none at all is taken as
    // a live session rather than ignored, so the button shows the agent is there.
    const bool initializing = (msg.is_object() && msg.value("method", "") == "initialize") ||
                              (msg.is_array() && std::any_of(msg.begin(), msg.end(), [](const json &m) {
                                   return m.is_object() && m.value("method", "") == "initialize";
                               }));
    // Without a session id only tool use counts (a bare ping is not an agent).
    const auto usesTools = [](const json &m) {
        return m.is_object() && m.value("method", "").rfind("tools/", 0) == 0;
    };
    const bool counts = !sessionId.isEmpty() || usesTools(msg) ||
                        (msg.is_array() && std::any_of(msg.begin(), msg.end(), usesTools));
    if(!initializing && counts) {
        auto it = m_sessions.find(sessionId);
        if(it == m_sessions.end()) {
            const QString client = m_knownClients.count(sessionId) ? m_knownClients.at(sessionId) : tr("MCP client");
            it = m_sessions.emplace(sessionId, Session{client, QDateTime::currentDateTime()}).first;
            m_log->add(McpEvent::Kind::Connect, client,
                       sessionId.isEmpty() ? tr("Connected (no session)") : tr("Session resumed"));
        }
        it->second.lastSeen = QDateTime::currentDateTime();
        m_lastSession = sessionId;
    }
    json out;
    bool anyReply = false;
    if(msg.is_array()) {
        out = json::array();
        for(const json &m : msg) {
            bool note = false;
            json reply = dispatch(m, sessionId, note);
            if(!note) out.push_back(std::move(reply));
        }
        anyReply = !out.empty();
    } else {
        bool note = false;
        out = dispatch(msg, sessionId, note);
        anyReply = !note;
    }
    std::map<QByteArray, QByteArray> extra;
    if(!sessionId.isEmpty()) extra["Mcp-Session-Id"] = sessionId.toLatin1();
    if(anyReply) respond(socket, 200, QByteArray::fromStdString(out.dump()), "application/json", extra);
    else respond(socket, 202, QByteArray(), "application/json", extra);
    updateState();
    emit activity();
}

json McpServer::dispatch(const json &msg, QString &sessionId, bool &isNotification) {
    if(!msg.is_object() || !msg.contains("method")) {
        isNotification = !msg.contains("id");
        return rpcError(msg.value("id", json()), -32600, "invalid request");
    }
    const std::string method = msg.value("method", "");
    isNotification = !msg.contains("id");
    const json id = msg.value("id", json());
    const json params = msg.value("params", json::object());
    auto ok = [&](json result) { return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}; };

    if(method == "initialize") {
        const json info = params.value("clientInfo", json::object());
        QString client = QString::fromStdString(info.value("name", "unknown client"));
        if(info.contains("version")) client += QLatin1Char(' ') + QString::fromStdString(info.value("version", ""));
        sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_sessions[sessionId] = {client, QDateTime::currentDateTime()};
        m_knownClients[sessionId] = client;
        m_lastSession = sessionId;
        std::string version = kProtocols[0];
        const std::string asked = params.value("protocolVersion", "");
        for(const char *p : kProtocols)
            if(asked == p) version = p;
        m_log->add(McpEvent::Kind::Connect, client, tr("Connected (MCP %1)").arg(QString::fromStdString(version)));
        return ok({{"protocolVersion", version},
                   {"capabilities", {{"tools", {{"listChanged", false}}}}},
                   {"serverInfo", {{"name", "cadjitsu"}, {"title", "Cadjitsu CAD"}, {"version", cad::version()}}},
                   {"instructions",
                    "Cadjitsu is a parametric CAD app for 3D-printable parts. Units are millimetres and degrees; Z is up "
                    "(the print direction). Call get_design first. Sketch, extrude (new bodies by default), combine, "
                    "fillet/chamfer/hole, then export_stl and check the printability report."}});
    }
    const QString client = clientOf(sessionId);
    if(method == "notifications/initialized" || method.rfind("notifications/", 0) == 0) return {};
    if(method == "ping") return ok(json::object());
    if(method == "tools/list") return ok({{"tools", m_tools->toolList()}});
    if(method == "tools/call") {
        const std::string name = params.value("name", "");
        const json args = params.value("arguments", json::object());
        m_log->add(McpEvent::Kind::Call, client, QString::fromStdString(name), shorten(args.dump()));
        json result;
        try {
            result = m_tools->callTool(name, args);
        } catch(const std::exception &e) {
            result = {{"content", {{{"type", "text"}, {"text", std::string("internal error: ") + e.what()}}}}, {"isError", true}};
        }
        const bool failed = result.value("isError", false);
        std::string text;
        for(const auto &c : result.value("content", json::array()))
            if(c.value("type", "") == "text") text += c.value("text", "");
            else if(c.value("type", "") == "image") text += "[image]";
        m_log->add(failed ? McpEvent::Kind::Error : McpEvent::Kind::Result, client,
                   QString::fromStdString(name) + (failed ? tr(" failed") : tr(" done")), shorten(text, 600));
        return ok(result);
    }
    if(isNotification) return {};
    return rpcError(id, -32601, "method not found: " + method);
}

void McpServer::respond(QTcpSocket *socket, int code, const QByteArray &body, const QByteArray &contentType,
                        const std::map<QByteArray, QByteArray> &extra) {
    static const std::map<int, const char *> reasons = {{200, "OK"}, {202, "Accepted"}, {400, "Bad Request"},
                                                        {401, "Unauthorized"}, {403, "Forbidden"}, {404, "Not Found"},
                                                        {405, "Method Not Allowed"}, {413, "Payload Too Large"},
                                                        {431, "Request Header Fields Too Large"}};
    QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + ' ' + (reasons.count(code) ? reasons.at(code) : "Error") + "\r\n";
    head += "Content-Type: " + contentType + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\n";
    for(const auto &[k, v] : extra) head += k + ": " + v + "\r\n";
    head += "\r\n";
    socket->write(head + body);
    socket->flush();
}

} // namespace cadjitsu
