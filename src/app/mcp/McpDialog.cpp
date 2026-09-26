#include "mcp/McpDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace cadly {

namespace {

QColor kindColor(McpEvent::Kind k) {
    switch(k) {
    case McpEvent::Kind::Server: return QColor(90, 100, 115);
    case McpEvent::Kind::Connect: return QColor(24, 160, 86);
    case McpEvent::Kind::Disconnect: return QColor(150, 120, 40);
    case McpEvent::Kind::Call: return QColor(38, 110, 196);
    case McpEvent::Kind::Result: return QColor(30, 140, 120);
    case McpEvent::Kind::Error: return QColor(200, 50, 45);
    case McpEvent::Kind::Auth: return QColor(215, 110, 20);
    }
    return QColor(90, 100, 115);
}

QString kindLabel(McpEvent::Kind k) {
    switch(k) {
    case McpEvent::Kind::Server: return QObject::tr("Server");
    case McpEvent::Kind::Connect: return QObject::tr("Connected");
    case McpEvent::Kind::Disconnect: return QObject::tr("Disconnected");
    case McpEvent::Kind::Call: return QObject::tr("Tool call");
    case McpEvent::Kind::Result: return QObject::tr("Result");
    case McpEvent::Kind::Error: return QObject::tr("Error");
    case McpEvent::Kind::Auth: return QObject::tr("Refused");
    }
    return {};
}

// A small round badge per event kind.
QIcon kindIcon(McpEvent::Kind k) {
    static std::map<int, QIcon> cache;
    auto it = cache.find(int(k));
    if(it != cache.end()) return it->second;
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = kindColor(k);
    p.setBrush(QColor(c.red(), c.green(), c.blue(), 45));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QRectF(2, 2, 24, 24));
    p.setBrush(c);
    p.drawEllipse(QRectF(9, 9, 10, 10));
    p.end();
    return cache[int(k)] = QIcon(pm);
}

} // namespace

McpDialog::McpDialog(McpServer &server, McpLog &log, QWidget *parent)
    : QDialog(parent), m_server(server), m_logModel(log) {
    setWindowTitle(tr("MCP Server"));
    setObjectName(QStringLiteral("mcpDialog"));
    resize(760, 560);
    setStyleSheet(QStringLiteral(
        "#mcpDialog { background: #f6f7f9; }"
        "QTabWidget::pane { border: 1px solid #d3d8df; border-radius: 6px; background: white; top: -1px; }"
        "QTabBar::tab { padding: 6px 16px; color: #3c4450; font-weight: 600; border: none; }"
        "QTabBar::tab:selected { color: #1a66c9; border-bottom: 2px solid #1a66c9; }"
        "QLineEdit, QComboBox, QPlainTextEdit { color: #10161f; background: white; border: 1px solid #c2c9d2;"
        " border-radius: 4px; padding: 3px 5px; selection-background-color: #9cc6ff; selection-color: #10161f; }"
        "QLineEdit:focus { border: 1px solid #1a66c9; }"
        "QPushButton { color: #1c2128; background: #ffffff; border: 1px solid #c2c9d2; border-radius: 4px; padding: 4px 12px; }"
        "QPushButton:hover { background: #eef4fc; border-color: #8fb3e3; }"
        "#mcpApply { background: #1a66c9; color: white; border: none; font-weight: 600; padding: 5px 18px; }"
        "#mcpApply:hover { background: #2474db; }"
        "QTreeWidget { color: #1c2128; background: white; border: 1px solid #d3d8df; border-radius: 4px;"
        " alternate-background-color: #f7f9fc; }"
        "QTreeWidget::item { padding: 3px 2px; }"
        "QTreeWidget::item:selected { background: #d6e6fb; color: #0f1a2a; }"
        "QHeaderView::section { background: #eef1f5; color: #3c4450; border: none; border-bottom: 1px solid #d3d8df;"
        " padding: 4px 6px; font-weight: 600; }"
        "QLabel, QCheckBox { color: #1c2128; }"
        "#mcpHint { color: #5a6270; }"));
    auto *v = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    v->addWidget(m_tabs);

    // --- Settings ---------------------------------------------------------------------
    auto *settings = new QWidget(m_tabs);
    auto *sv = new QVBoxLayout(settings);
    sv->setContentsMargins(16, 14, 16, 14);
    auto *intro = new QLabel(tr("Let AI agents (Claude Code, Claude Desktop, Hermes...) build and edit designs in "
                                "Cadly through the Model Context Protocol. The server only listens on this computer "
                                "(127.0.0.1) and every request must carry the token."),
                             settings);
    intro->setWordWrap(true);
    intro->setObjectName(QStringLiteral("mcpHint"));
    sv->addWidget(intro);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    const McpSettings s = server.settings().token.isEmpty() ? McpSettings::load() : server.settings();
    m_enabled = new QCheckBox(tr("Enable the MCP server"), settings);
    m_enabled->setObjectName(QStringLiteral("mcpEnabled"));
    m_enabled->setChecked(s.enabled);
    form->addRow(QString(), m_enabled);
    m_port = new QSpinBox(settings);
    m_port->setObjectName(QStringLiteral("mcpPort"));
    m_port->setRange(1024, 65535);
    m_port->setValue(s.port);
    m_port->setMaximumWidth(120);
    form->addRow(tr("Port"), m_port);
    auto *tokenRow = new QHBoxLayout;
    m_token = new QLineEdit(s.token, settings);
    m_token->setObjectName(QStringLiteral("mcpToken"));
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setMinimumWidth(320);
    m_show = new QPushButton(tr("Show"), settings);
    m_generate = new QPushButton(tr("Generate"), settings);
    m_generate->setObjectName(QStringLiteral("mcpGenerate"));
    m_copyToken = new QPushButton(tr("Copy"), settings);
    tokenRow->addWidget(m_token, 1);
    tokenRow->addWidget(m_show);
    tokenRow->addWidget(m_generate);
    tokenRow->addWidget(m_copyToken);
    form->addRow(tr("Auth token"), tokenRow);
    m_status = new QLabel(settings);
    m_status->setObjectName(QStringLiteral("mcpStatus"));
    m_status->setTextFormat(Qt::RichText);
    form->addRow(tr("Status"), m_status);
    sv->addLayout(form);

    auto *connectTitle = new QLabel(tr("<b>Connect an agent</b>"), settings);
    sv->addSpacing(8);
    sv->addWidget(connectTitle);
    auto *clientRow = new QHBoxLayout;
    m_client = new QComboBox(settings);
    m_client->addItems({tr("Claude Code (command)"), tr("Claude Code plugin (environment)"), tr("Claude Desktop (config)"),
                        tr("Hermes Agent"), tr("Hermes Agent (manual config)"), tr("URL and header")});
    auto *copySnippet = new QPushButton(tr("Copy"), settings);
    clientRow->addWidget(m_client, 1);
    clientRow->addWidget(copySnippet);
    sv->addLayout(clientRow);
    m_snippet = new QPlainTextEdit(settings);
    m_snippet->setReadOnly(true);
    m_snippet->setObjectName(QStringLiteral("mcpSnippet"));
    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(11);
    m_snippet->setFont(mono);
    m_snippet->setMaximumHeight(96);
    sv->addWidget(m_snippet);
    auto *skillHint = new QLabel(tr("The Cadly skill (how to model printable parts with these tools) installs from "
                                    "GitHub: Claude Code <code>/plugin marketplace add RidgebackZulu/cadly</code>; Hermes "
                                    "<code>hermes plugins install RidgebackZulu/cadly/plugins/cadly</code>."),
                                 settings);
    skillHint->setWordWrap(true);
    skillHint->setObjectName(QStringLiteral("mcpHint"));
    skillHint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sv->addWidget(skillHint);
    sv->addStretch();
    auto *applyRow = new QHBoxLayout;
    applyRow->addStretch();
    m_apply = new QPushButton(tr("Apply"), settings);
    m_apply->setObjectName(QStringLiteral("mcpApply"));
    applyRow->addWidget(m_apply);
    sv->addLayout(applyRow);
    m_tabs->addTab(settings, tr("Settings"));

    // --- Event log --------------------------------------------------------------------
    auto *logPage = new QWidget(m_tabs);
    auto *lv = new QVBoxLayout(logPage);
    lv->setContentsMargins(12, 12, 12, 12);
    auto *top = new QHBoxLayout;
    m_filter = new QLineEdit(logPage);
    m_filter->setPlaceholderText(tr("Filter events..."));
    m_filter->setClearButtonEnabled(true);
    auto *clear = new QPushButton(tr("Clear view"), logPage);
    auto *openFolder = new QPushButton(tr("Open log folder"), logPage);
    openFolder->setObjectName(QStringLiteral("mcpOpenLogFolder"));
    top->addWidget(m_filter, 1);
    top->addWidget(clear);
    top->addWidget(openFolder);
    lv->addLayout(top);
    auto *split = new QSplitter(Qt::Vertical, logPage);
    m_log = new QTreeWidget(split);
    m_log->setObjectName(QStringLiteral("mcpLog"));
    m_log->setHeaderLabels({tr("Time"), tr("Event"), tr("Client"), tr("Summary")});
    m_log->setRootIsDecorated(false);
    m_log->setAlternatingRowColors(true);
    m_log->setIconSize(QSize(14, 14));
    m_log->setUniformRowHeights(true);
    m_log->header()->setStretchLastSection(true);
    m_log->setColumnWidth(0, 80);
    m_log->setColumnWidth(1, 110);
    m_log->setColumnWidth(2, 140);
    m_detail = new QPlainTextEdit(split);
    m_detail->setReadOnly(true);
    m_detail->setFont(mono);
    m_detail->setPlaceholderText(tr("Select an event to see its details."));
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    lv->addWidget(split, 1);
    auto *path = new QLabel(tr("Saved to %1 (rolling, at most 1 MB)").arg(QFileInfo(log.filePath()).absoluteFilePath()), logPage);
    path->setObjectName(QStringLiteral("mcpHint"));
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lv->addWidget(path);
    m_tabs->addTab(logPage, tr("Event log"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    for(const McpEvent &e : log.events()) addEvent(e);
    m_log->scrollToBottom();

    connect(m_show, &QPushButton::clicked, this, [this] {
        const bool hidden = m_token->echoMode() == QLineEdit::Password;
        m_token->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        m_show->setText(hidden ? tr("Hide") : tr("Show"));
    });
    connect(m_generate, &QPushButton::clicked, this, [this] {
        m_token->setText(McpSettings::generateToken());
        updateSnippet();
    });
    connect(m_copyToken, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_token->text()); });
    connect(copySnippet, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(snippet()); });
    connect(m_client, &QComboBox::currentIndexChanged, this, &McpDialog::updateSnippet);
    connect(m_port, &QSpinBox::valueChanged, this, &McpDialog::updateSnippet);
    connect(m_token, &QLineEdit::textChanged, this, &McpDialog::updateSnippet);
    connect(m_apply, &QPushButton::clicked, this, &McpDialog::apply);
    connect(&server, &McpServer::stateChanged, this, &McpDialog::updateStatus);
    connect(&log, &McpLog::eventAdded, this, [this](const McpEvent &e) {
        addEvent(e);
        m_log->scrollToBottom();
    });
    connect(&log, &McpLog::cleared, m_log, &QTreeWidget::clear);
    connect(clear, &QPushButton::clicked, &log, &McpLog::clearView);
    connect(openFolder, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_logModel.directory())); });
    connect(m_filter, &QLineEdit::textChanged, this, &McpDialog::applyFilter);
    connect(m_log, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        m_detail->setPlainText(it ? it->data(0, Qt::UserRole).toString() : QString());
    });
    updateSnippet();
    updateStatus();
}

void McpDialog::showLog() { m_tabs->setCurrentIndex(1); }

McpSettings McpDialog::edited() const {
    McpSettings s;
    s.enabled = m_enabled->isChecked();
    s.port = m_port->value();
    s.token = m_token->text().trimmed();
    return s;
}

void McpDialog::apply() {
    McpSettings s = edited();
    if(s.token.size() < 16) {
        s.token = McpSettings::generateToken();
        m_token->setText(s.token);
    }
    s.save();
    m_server.apply(s);
    updateStatus();
}

void McpDialog::updateStatus() {
    QString text;
    switch(m_server.state()) {
    case McpServer::State::Off:
        text = m_server.error().isEmpty()
                   ? tr("<span style='color:#6b7380'>● Off</span>")
                   : tr("<span style='color:#c8322d'>● Could not start: %1</span>").arg(m_server.error().toHtmlEscaped());
        break;
    case McpServer::State::Listening:
        text = tr("<span style='color:#1a66c9'>● Listening on %1</span> — no agent connected")
                   .arg(QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_server.port()));
        break;
    case McpServer::State::Connected:
        text = tr("<span style='color:#18a05a'><b>● Connected</b></span> — %1 on port %2")
                   .arg(m_server.clientName().toHtmlEscaped())
                   .arg(m_server.port());
        break;
    }
    m_status->setText(text);
}

QString McpDialog::snippet() const { return m_snippet->toPlainText(); }

void McpDialog::updateSnippet() {
    const QString url = QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_port->value());
    const QString token = m_token->text().trimmed();
    QString text;
    switch(m_client->currentIndex()) {
    case 0:
        text = QStringLiteral("claude mcp add --transport http cadly %1 --header \"Authorization: Bearer %2\"").arg(url, token);
        break;
    case 1:
        text = QStringLiteral("# With the Cadly plugin (/plugin install cadly@cadly), set before starting Claude Code:\n"
                              "export CADLY_MCP_TOKEN=%1\nexport CADLY_MCP_URL=%2")
                   .arg(token, url);
        break;
    case 2:
        text = QStringLiteral("{\n  \"mcpServers\": {\n    \"cadly\": {\n      \"command\": \"npx\",\n      \"args\": [\"-y\", "
                              "\"mcp-remote\", \"%1\", \"--header\", \"Authorization: Bearer %2\"]\n    }\n  }\n}")
                   .arg(url, token);
        break;
    case 3:
        // The plugin brings the skill; `hermes mcp add` connects, stores the token in Hermes' secrets
        // and enables the tools.
        text = QStringLiteral("hermes plugins install RidgebackZulu/cadly/plugins/cadly --enable\n"
                              "hermes mcp add cadly --url %1 --auth header\n"
                              "# When asked for the Bearer token, paste: %2")
                   .arg(url, token);
        break;
    case 4:
        text = QStringLiteral("# ~/.hermes/config.yaml\nmcp_servers:\n  cadly:\n    url: \"%1\"\n    headers:\n"
                              "      Authorization: \"Bearer %2\"")
                   .arg(url, token);
        break;
    default: text = QStringLiteral("%1\nAuthorization: Bearer %2").arg(url, token); break;
    }
    m_snippet->setPlainText(text);
}

void McpDialog::addEvent(const McpEvent &e) {
    auto *it = new QTreeWidgetItem(m_log);
    it->setText(0, e.time.toString(QStringLiteral("HH:mm:ss")));
    it->setText(1, kindLabel(e.kind));
    it->setIcon(1, kindIcon(e.kind));
    it->setForeground(1, kindColor(e.kind));
    it->setText(2, e.client);
    it->setText(3, e.summary);
    it->setToolTip(3, e.detail.isEmpty() ? e.summary : e.detail.left(500));
    it->setData(0, Qt::UserRole,
                QStringLiteral("%1  %2  %3\n%4\n\n%5")
                    .arg(e.time.toString(Qt::ISODateWithMs), kindLabel(e.kind), e.client, e.summary, e.detail));
    if(e.kind == McpEvent::Kind::Error || e.kind == McpEvent::Kind::Auth) {
        QFont f = it->font(3);
        f.setBold(true);
        it->setFont(3, f);
    }
    if(!m_filter->text().isEmpty()) applyFilter();
}

void McpDialog::applyFilter() {
    const QString f = m_filter->text().trimmed();
    for(int i = 0; i < m_log->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_log->topLevelItem(i);
        bool match = f.isEmpty();
        for(int c = 1; !match && c < 4; ++c) match = it->text(c).contains(f, Qt::CaseInsensitive);
        it->setHidden(!match);
    }
}

} // namespace cadly
