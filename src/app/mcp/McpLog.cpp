#include "mcp/McpLog.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace cadly {

namespace {

constexpr size_t kKeepInView = 500;
constexpr qint64 kKeepOnTrim = 768 * 1024;

} // namespace

QString McpEvent::kindName(Kind k) {
    switch(k) {
    case Kind::Server: return QStringLiteral("server");
    case Kind::Connect: return QStringLiteral("connect");
    case Kind::Disconnect: return QStringLiteral("disconnect");
    case Kind::Call: return QStringLiteral("call");
    case Kind::Result: return QStringLiteral("result");
    case Kind::Error: return QStringLiteral("error");
    case Kind::Auth: return QStringLiteral("auth");
    }
    return QStringLiteral("server");
}

McpEvent::Kind McpEvent::kindFromName(const QString &name) {
    for(Kind k : {Kind::Server, Kind::Connect, Kind::Disconnect, Kind::Call, Kind::Result, Kind::Error, Kind::Auth})
        if(kindName(k) == name) return k;
    return Kind::Server;
}

McpLog::McpLog(QObject *parent) : QObject(parent) {
    setDirectory(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("logs")));
}

void McpLog::setDirectory(const QString &dir) {
    m_dir = dir;
    QDir().mkpath(dir);
    m_events.clear();
    loadRecent();
}

QString McpLog::filePath() const { return QDir(m_dir).filePath(QStringLiteral("mcp.log")); }

void McpLog::add(McpEvent::Kind kind, const QString &client, const QString &summary, const QString &detail) {
    McpEvent e;
    e.time = QDateTime::currentDateTime();
    e.kind = kind;
    e.client = client;
    e.summary = summary;
    e.detail = detail.size() > 2000 ? detail.left(2000) + QStringLiteral("…") : detail;
    m_events.push_back(e);
    while(m_events.size() > kKeepInView) m_events.pop_front();
    write(e);
    emit eventAdded(e);
}

void McpLog::clearView() {
    m_events.clear();
    emit cleared();
}

void McpLog::write(const McpEvent &e) {
    QJsonObject o{{QStringLiteral("time"), e.time.toString(Qt::ISODateWithMs)},
                  {QStringLiteral("kind"), McpEvent::kindName(e.kind)},
                  {QStringLiteral("client"), e.client},
                  {QStringLiteral("summary"), e.summary},
                  {QStringLiteral("detail"), e.detail}};
    const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
    QFile f(filePath());
    // Roll: keep the newest part (whole lines) when the file would pass the limit.
    if(f.exists() && f.size() + line.size() > maxBytes() && f.open(QIODevice::ReadOnly)) {
        QByteArray all = f.readAll();
        f.close();
        const qint64 keep = std::min<qint64>(kKeepOnTrim, maxBytes() - line.size());
        if(all.size() > keep) {
            all = all.right(keep);
            const int nl = all.indexOf('\n');
            all = nl >= 0 ? all.mid(nl + 1) : QByteArray();
        }
        if(f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(all);
            f.close();
        }
    }
    if(f.open(QIODevice::Append)) {
        f.write(line);
        f.close();
    }
}

void McpLog::loadRecent() {
    QFile f(filePath());
    if(!f.open(QIODevice::ReadOnly)) return;
    const qint64 size = f.size();
    if(size > 256 * 1024) f.seek(size - 256 * 1024);
    const QList<QByteArray> lines = f.readAll().split('\n');
    for(const QByteArray &l : lines) {
        const QJsonObject o = QJsonDocument::fromJson(l).object();
        if(o.isEmpty()) continue;
        McpEvent e;
        e.time = QDateTime::fromString(o.value(QStringLiteral("time")).toString(), Qt::ISODateWithMs);
        e.kind = McpEvent::kindFromName(o.value(QStringLiteral("kind")).toString());
        e.client = o.value(QStringLiteral("client")).toString();
        e.summary = o.value(QStringLiteral("summary")).toString();
        e.detail = o.value(QStringLiteral("detail")).toString();
        m_events.push_back(e);
    }
    while(m_events.size() > kKeepInView) m_events.pop_front();
}

} // namespace cadly
