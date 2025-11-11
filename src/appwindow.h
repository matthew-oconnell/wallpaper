#pragma once

#include <QWidget>
#include "wallpapersetter.h"
#include "redditfetcher.h"
#include "cachemanager.h"
#include "thumbnailviewer.h"

class QSystemTrayIcon;

class QLabel;
class QPushButton;


class AppWindow : public QWidget {
    Q_OBJECT
public:
    explicit AppWindow(QWidget *parent = nullptr);
    ~AppWindow();

private slots:
    void onNewRandom();
    void onThumbnailSelected(const QString &imagePath);
    void onThumbUp();
    void onThumbDown();
    void onPermaban();

private:
    QSystemTrayIcon *trayIcon_ = nullptr;
    WallpaperSetter wallpaperSetter_;
    RedditFetcher m_fetcher;
    CacheManager m_cache;
    ThumbnailViewer *thumbnailViewer_ = nullptr;
    QString currentSelectedPath_;
    // detail panel widgets
    QLabel *detailPath_ = nullptr;
    QLabel *detailSubreddit_ = nullptr;
    QLabel *detailResolution_ = nullptr;
    QLabel *detailScore_ = nullptr;
    QLabel *detailBanned_ = nullptr;
    QPushButton *btnThumbUp_ = nullptr;
    QPushButton *btnThumbDown_ = nullptr;
    QPushButton *btnPermaban_ = nullptr;
};
