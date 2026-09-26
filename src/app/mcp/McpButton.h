#pragma once

#include "mcp/McpServer.h"

#include <QAbstractButton>

class QVariantAnimation;

namespace cadly {

// The "MCP" pill in the toolbar: grey when the server is off, blue when it
// listens, green with a soft pulsing glow while an agent is connected.
// Clicking it opens the MCP dialog.
class McpButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit McpButton(QWidget *parent = nullptr);

    void setState(McpServer::State s, const QString &client = {});
    McpServer::State state() const { return m_state; }
    double glow() const { return m_glow; }
    QSize sizeHint() const override { return QSize(78, 26); }

protected:
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    McpServer::State m_state = McpServer::State::Off;
    QVariantAnimation *m_pulse;
    double m_glow = 0.0;
    bool m_hover = false;
};

} // namespace cadly
