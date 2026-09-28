#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>

#include <nlohmann/json.hpp>

#include <deque>
#include <functional>
#include <map>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace cadjitsu {

class McpLog;

// Settings > MCP server: off by default; localhost only, bearer token.
struct McpSettings {
    bool enabled = false;
    int port = 7823;
    QString token;

    static McpSettings load();
    void save() const;
    static QString generateToken(); // 32 random bytes, base64url (43 characters)
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/mcp").arg(port); }
};

// The tools an MCP client can call (implemented by McpTools).
class McpToolProvider {
public:
    virtual ~McpToolProvider() = default;
    virtual nlohmann::json toolList() const = 0;
    // The MCP tools/call result ({content: [...], isError}).
    virtual nlohmann::json callTool(const std::string &name, const nlohmann::json &args) = 0;
};

// Cadjitsu's MCP server: the "Streamable HTTP" transport (JSON-RPC 2.0 over
// POST /mcp, answered with plain JSON) on 127.0.0.1. Every request needs
// "Authorization: Bearer <token>". Requests are handled one at a time on the
// UI thread (tools edit the open design like the user does).
class McpServer : public QObject {
    Q_OBJECT

public:
    enum class State { Off, Listening, Connected };

    McpServer(McpLog *log, McpToolProvider *tools, QObject *parent = nullptr);
    ~McpServer() override;

    // Applies settings: starts, restarts or stops. False (with error()) if the
    // port cannot be opened.
    bool apply(const McpSettings &s);
    void stop();
    const McpSettings &settings() const { return m_settings; }
    State state() const { return m_state; }
    bool listening() const;
    quint16 port() const;
    QString error() const { return m_error; }
    // The client of the most recently active session ("Claude Code 2.1").
    QString clientName() const;
    int requestCount() const { return m_requests; }

signals:
    void stateChanged(cadjitsu::McpServer::State state);
    // A request was handled (the button pulses once).
    void activity();

private:
    struct Session {
        QString client;
        QDateTime lastSeen;
    };
    struct Request {
        QString method, path;
        std::map<QString, QString> headers; // lower-case names
        QByteArray body;
    };

    void onConnection();
    void onReadyRead(QTcpSocket *socket);
    void handle(QTcpSocket *socket, const Request &r);
    void processQueue();
    nlohmann::json dispatch(const nlohmann::json &msg, QString &sessionId, bool &isNotification);
    void respond(QTcpSocket *socket, int code, const QByteArray &body, const QByteArray &contentType = "application/json",
                 const std::map<QByteArray, QByteArray> &extra = {});
    void updateState();
    QString clientOf(const QString &sessionId) const;

    McpLog *m_log;
    McpToolProvider *m_tools;
    McpSettings m_settings;
    QTcpServer *m_server = nullptr;
    QTimer *m_idle = nullptr;
    State m_state = State::Off;
    QString m_error;
    std::map<QString, Session> m_sessions;
    QString m_lastSession;
    // Client names by session id; kept across restarts so resumed sessions are named.
    std::map<QString, QString> m_knownClients;
    std::map<QTcpSocket *, QByteArray> m_buffers;
    std::deque<std::pair<QPointer<QTcpSocket>, Request>> m_queue;
    bool m_busy = false;
    int m_requests = 0;
};

} // namespace cadjitsu
