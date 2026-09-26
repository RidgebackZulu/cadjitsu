#pragma once

#include "doc/Document.h"

#include <QMainWindow>

#include <memory>

class QLabel;

namespace cadly {

class ModelView;
class Viewport;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    Viewport *viewport() const { return m_viewport; }
    ModelView *modelView() const { return m_modelView; }
    cad::Document &document() { return *m_document; }
    QLabel *selectionStatsLabel() const { return m_selectionStats; }

    // Re-reads the document into the canvas (after programmatic edits).
    void refresh();

    bool openFile(const QString &path);
    bool saveFile(const QString &path);

private:
    void buildMenus();
    void updateTitle();

    std::unique_ptr<cad::Document> m_document;
    Viewport *m_viewport = nullptr;
    ModelView *m_modelView = nullptr;
    QLabel *m_selectionStats = nullptr;
    QString m_path;
};

} // namespace cadly
