#pragma once

#include <QWidget>
#include "wallpapersetter.h"
#include "redditfetcher.h"
#include "cachemanager.h"
#include "thumbnailviewer.h"
#include "sourcespanel.h"
#include "filterspanel.h"

class QSystemTrayIcon;

class QLabel;
class QPushButton;
class QCheckBox;


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
    void onUpdateCache();

private:
    QSystemTrayIcon *trayIcon_ = nullptr;
    WallpaperSetter wallpaperSetter_;
    RedditFetcher m_fetcher;
    CacheManager m_cache;
    ThumbnailViewer *thumbnailViewer_ = nullptr;
    QString currentSelectedPath_;
    QStringList subscribedSubreddits_ = { "WidescreenWallpaper" };
    QPushButton *btnUpdate_ = nullptr;
    SourcesPanel *sourcesPanel_ = nullptr;
    FiltersPanel *filtersPanel_ = nullptr;
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
