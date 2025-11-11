#include "appwindow.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QAction>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QRandomGenerator>
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
    qDebug() << "Fetching new random wallpaper...";
    
    // Fetch image URLs from Reddit
    std::vector<std::string> urls = m_fetcher.fetchRecentImageUrls("WidescreenWallpaper", 10);
    
    if (urls.empty()) {
        qWarning() << "No image URLs found";
        return;
    }
    
    // Pick a random URL
    int randomIndex = QRandomGenerator::global()->bounded(static_cast<int>(urls.size()));
    QString selectedUrl = QString::fromStdString(urls[randomIndex]);
    
    qDebug() << "Selected URL:" << selectedUrl;
    
    // Download and cache the image
    QString cachedPath = m_cache.downloadAndCache(selectedUrl);
    
    if (cachedPath.isEmpty()) {
        qWarning() << "Failed to download image";
        return;
    }
    
    qDebug() << "Image cached at:" << cachedPath;
    
    // Set the wallpaper using the cached image
    if (wallpaperSetter_.setWallpaper(cachedPath)) {
        qDebug() << "Wallpaper set successfully!";
    } else {
        qWarning() << "Failed to set wallpaper";
    }
}
