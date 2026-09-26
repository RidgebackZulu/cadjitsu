#include "mcp/McpButton.h"

#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QRadialGradient>
#include <QTimer>

#include <cmath>

namespace cadly {

namespace {
constexpr double kSteadyGlow = 0.35; // connected, idle
constexpr int kPulseMs = 900;
constexpr int kFrameMs = 33;          // at most ~30 repaints a second, only while pulsing
} // namespace

McpButton::McpButton(QWidget *parent) : QAbstractButton(parent) {
    setObjectName(QStringLiteral("mcpButton"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(sizeHint());
    m_frame = new QTimer(this);
    m_frame->setInterval(kFrameMs);
    m_frame->setTimerType(Qt::CoarseTimer);
    connect(m_frame, &QTimer::timeout, this, [this] {
        const double t = double(QDateTime::currentMSecsSinceEpoch() - m_pulseStart) / kPulseMs;
        if(t >= 1.0 || m_state != McpServer::State::Connected) {
            m_frame->stop();
            m_pulseStart = -1;
            m_glow = m_state == McpServer::State::Connected ? kSteadyGlow : 0.0;
        } else {
            // Up and back down to the steady level.
            m_glow = kSteadyGlow + (1.0 - kSteadyGlow) * std::sin(t * M_PI);
        }
        update();
    });
    setState(McpServer::State::Off);
}

void McpButton::setState(McpServer::State s, const QString &client) {
    m_state = s;
    switch(s) {
    case McpServer::State::Off:
        setToolTip(tr("MCP server off: click to set it up and let AI agents use Cadly"));
        break;
    case McpServer::State::Listening:
        setToolTip(tr("MCP server listening, no agent connected: click for settings and the event log"));
        break;
    case McpServer::State::Connected:
        setToolTip(tr("Agent connected%1: click for settings and the event log")
                       .arg(client.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(client)));
        break;
    }
    if(s != McpServer::State::Connected) {
        m_frame->stop();
        m_pulseStart = -1;
    }
    m_glow = s == McpServer::State::Connected ? kSteadyGlow : 0.0;
    update();
}

void McpButton::pulse() {
    if(m_state != McpServer::State::Connected) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Coalesce: a pulse under way is not restarted before its peak.
    if(m_pulseStart >= 0 && now - m_pulseStart < kPulseMs / 2) return;
    m_pulseStart = now;
    if(!m_frame->isActive()) m_frame->start();
}

bool McpButton::animating() const { return m_frame->isActive(); }

void McpButton::enterEvent(QEnterEvent *e) {
    m_hover = true;
    update();
    QAbstractButton::enterEvent(e);
}

void McpButton::leaveEvent(QEvent *e) {
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void McpButton::paintEvent(QPaintEvent *) {
    ++m_paints;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF pill = QRectF(rect()).adjusted(5, 4, -5, -4);
    const double r = pill.height() / 2;
    QColor accent, fill, text;
    switch(m_state) {
    case McpServer::State::Off:
        accent = QColor(150, 157, 168);
        fill = QColor(245, 246, 248);
        text = QColor(96, 103, 114);
        break;
    case McpServer::State::Listening:
        accent = QColor(26, 102, 201); // the theme's accent
        fill = QColor(234, 242, 252);
        text = QColor(20, 66, 140);
        break;
    case McpServer::State::Connected:
        accent = QColor(30, 154, 84);
        fill = QColor(227, 246, 235);
        text = QColor(14, 92, 50);
        break;
    }
    // The glow: a steady halo while an agent is connected, brighter during a pulse.
    if(m_state == McpServer::State::Connected) {
        const double spread = 4.0 + 3.0 * m_glow;
        for(int i = 3; i >= 1; --i) {
            QColor c = accent;
            c.setAlphaF(0.10 + 0.16 * m_glow / i);
            QPainterPath halo;
            halo.addRoundedRect(pill.adjusted(-spread * i / 3, -spread * i / 3, spread * i / 3, spread * i / 3), r + spread * i / 3,
                                r + spread * i / 3);
            p.fillPath(halo, c);
        }
    }
    QPainterPath body;
    body.addRoundedRect(pill, r, r);
    p.fillPath(body, m_hover || isDown() ? fill.darker(106) : fill);
    p.setPen(QPen(accent, 1.4));
    p.drawPath(body);
    // Status dot.
    const QPointF dot(pill.left() + r + 1, pill.center().y());
    if(m_state == McpServer::State::Connected) {
        QRadialGradient g(dot, 6);
        QColor c = accent.lighter(130);
        c.setAlphaF(0.6 + 0.4 * m_glow);
        g.setColorAt(0, c);
        g.setColorAt(1, QColor(accent.red(), accent.green(), accent.blue(), 0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(dot, 6, 6);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(m_state == McpServer::State::Off ? QColor(185, 190, 198) : accent);
    p.drawEllipse(dot, 3.2, 3.2);
    QFont f = font();
    f.setBold(true);
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(text);
    p.drawText(pill.adjusted(r * 2 + 2, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("MCP"));
}

} // namespace cadly
