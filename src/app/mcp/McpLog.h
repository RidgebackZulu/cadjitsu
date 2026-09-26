#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

#include <deque>

namespace cadly {

// One thing that happened on the MCP server.
struct McpEvent {
    enum class Kind { Server, Connect, Disconnect, Call, Result, Error, Auth };
    QDateTime time;
    Kind kind = Kind::Server;
    QString client;  // "Claude Code 2.1", empty for server events
    QString summary; // one line
    QString detail;  // arguments / result, shortened

    static QString kindName(Kind k);
    static Kind kindFromName(const QString &name);
};

// The MCP event log: kept in memory for the dialog and appended to a rolling
// file (one JSON object per line) that never grows past maxBytes(); when it
// would, the oldest lines are dropped.
class McpLog : public QObject {
    Q_OBJECT

public:
    explicit McpLog(QObject *parent = nullptr);

    void add(McpEvent::Kind kind, const QString &client, const QString &summary, const QString &detail = {});
    const std::deque<McpEvent> &events() const { return m_events; }
    void clearView();

    // Where the log file lives (<app data>/logs/mcp.log unless overridden).
    QString directory() const { return m_dir; }
    QString filePath() const;
    void setDirectory(const QString &dir); // tests
    static constexpr qint64 maxBytes() { return 1024 * 1024; }

signals:
    void eventAdded(const cadly::McpEvent &e);
    void cleared();

private:
    void write(const McpEvent &e);
    void loadRecent();

    QString m_dir;
    std::deque<McpEvent> m_events;
};

} // namespace cadly
