#pragma once

#include "mcp/McpServer.h"

#include <QAbstractButton>

class QTimer;

namespace cadjitsu {

// The "MCP" pill in the toolbar: grey when the server is off, blue when it
// listens, green with a steady soft glow while an agent is connected. Each
// request it handles makes it pulse once (briefly, at most 30 frames a
// second); otherwise it never repaints on its own. Clicking it opens the MCP
// dialog.
class McpButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit McpButton(QWidget *parent = nullptr);

    void setState(McpServer::State s, const QString &client = {});
    McpServer::State state() const { return m_state; }
    double glow() const { return m_glow; }
    // One short pulse (a burst of requests stays one pulse).
    void pulse();
    bool animating() const;
    int paintCount() const { return m_paints; }
    QSize sizeHint() const override { return QSize(78, 26); }

protected:
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    McpServer::State m_state = McpServer::State::Off;
    QTimer *m_frame;
    qint64 m_pulseStart = -1;
    double m_glow = 0.0;
    int m_paints = 0;
    bool m_hover = false;
};

} // namespace cadjitsu
