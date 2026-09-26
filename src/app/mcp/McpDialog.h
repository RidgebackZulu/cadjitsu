#pragma once

#include "mcp/McpLog.h"
#include "mcp/McpServer.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace cadly {

// The MCP server dialog (the toolbar's MCP button, File > MCP Server):
// Settings (on / off, port, token, how to connect an agent) and the live
// event log (also written to a rolling file; a button opens its folder).
class McpDialog : public QDialog {
    Q_OBJECT

public:
    McpDialog(McpServer &server, McpLog &log, QWidget *parent = nullptr);

    void showLog();
    QCheckBox *enabledBox() const { return m_enabled; }
    QSpinBox *portBox() const { return m_port; }
    QLineEdit *tokenEdit() const { return m_token; }
    QPushButton *generateButton() const { return m_generate; }
    QPushButton *applyButton() const { return m_apply; }
    QTreeWidget *logView() const { return m_log; }
    QLabel *statusLabel() const { return m_status; }
    QString snippet() const;

private:
    void apply();
    void updateStatus();
    void updateSnippet();
    void addEvent(const McpEvent &e);
    void applyFilter();
    McpSettings edited() const;

    McpServer &m_server;
    McpLog &m_logModel;
    QTabWidget *m_tabs;
    QCheckBox *m_enabled;
    QSpinBox *m_port;
    QLineEdit *m_token;
    QPushButton *m_show, *m_generate, *m_copyToken, *m_apply;
    QLabel *m_status;
    QComboBox *m_client;
    QPlainTextEdit *m_snippet;
    QTreeWidget *m_log;
    QPlainTextEdit *m_detail;
    QLineEdit *m_filter;
};

} // namespace cadly
