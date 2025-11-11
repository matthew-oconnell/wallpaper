#pragma once

#include <QWidget>

class QSystemTrayIcon;

class AppWindow : public QWidget {
    Q_OBJECT
public:
    explicit AppWindow(QWidget *parent = nullptr);
    ~AppWindow();

private slots:
    void onNewRandom();

private:
    QSystemTrayIcon *trayIcon_ = nullptr;
};
