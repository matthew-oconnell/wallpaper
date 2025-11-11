#include "appwindow.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QAction>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include "redditfetcher.h"
#include "cachemanager.h"

AppWindow::AppWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Wallpaper C++");
    resize(320, 120);

    QVBoxLayout *l = new QVBoxLayout(this);
    QLabel *label = new QLabel("Subreddit: r/WidescreenWallpaper", this);
    l->addWidget(label);
    QPushButton *btn = new QPushButton("New Random Wallpaper", this);
    connect(btn, &QPushButton::clicked, this, &AppWindow::onNewRandom);
    l->addWidget(btn);

    // tray
    trayIcon_ = new QSystemTrayIcon(QIcon::fromTheme("image-x-generic"), this);
    QMenu *menu = new QMenu();
    QAction *actNew = new QAction("New Random Wallpaper", this);
    connect(actNew, &QAction::triggered, this, &AppWindow::onNewRandom);
    menu->addAction(actNew);
    QAction *actQuit = new QAction("Quit", this);
    connect(actQuit, &QAction::triggered, QApplication::instance(), &QApplication::quit);
    menu->addSeparator();
    menu->addAction(actQuit);
    trayIcon_->setContextMenu(menu);
    trayIcon_->show();
}

AppWindow::~AppWindow() {
}

void AppWindow::onNewRandom() {
    // For now, synchronous simple demo: fetch URLs, download one, add to cache
    RedditFetcher fetcher;
    auto urls = fetcher.fetchRecentImageUrls("WidescreenWallpaper", 10);
    if (urls.empty()) return;
    CacheManager cache;
    // choose random
    std::string pick = urls[rand() % urls.size()];
    auto path = cache.downloadAndCache(QString::fromStdString(pick));
    trayIcon_->showMessage("Wallpaper", QString("Downloaded: %1").arg(path));
}
