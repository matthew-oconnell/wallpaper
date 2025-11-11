#pragma once

#include <QWidget>
#include "wallpapersetter.h"
#include "redditfetcher.h"
#include "cachemanager.h"

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
    WallpaperSetter wallpaperSetter_;
    RedditFetcher m_fetcher;
    CacheManager m_cache;
};
