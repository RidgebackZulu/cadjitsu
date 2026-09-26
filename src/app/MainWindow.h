#pragma once

#include <QMainWindow>

class QLabel;

namespace cadly {

class Viewport;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    Viewport *viewport() const { return m_viewport; }

private:
    Viewport *m_viewport = nullptr;
    QLabel *m_selectionStats = nullptr;
};

} // namespace cadly
