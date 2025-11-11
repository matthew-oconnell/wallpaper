#pragma once

#include <QWidget>
#include <QSystemTrayIcon>
#include "wallpapersetter.h"
#include "redditfetcher.h"
#include "cachemanager.h"
#include "thumbnailviewer.h"
#include "sourcespanel.h"
#include "filterspanel.h"

class QLabel;
class QPushButton;
class QCheckBox;
class QAction;
class QSpinBox;


class AppWindow : public QWidget {
    Q_OBJECT
public:
    explicit AppWindow(QWidget *parent = nullptr);
    ~AppWindow();

private slots:
    void onNewRandom();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onThumbnailSelected(const QString &imagePath);
    void onToggleFavorite();
    void onThumbnailFavoriteRequested(const QString &imagePath);
    void onRandomFavorite();
    void onPermaban();
    void onThumbnailPermabanRequested(const QString &imagePath);
    void onUpdateCache();
    void startCleanup();
    void cleanupFinished();

private:
    QSystemTrayIcon *trayIcon_ = nullptr;
    QAction *trayActFavorite_ = nullptr;
    QAction *trayActRandomFavorite_ = nullptr;
    QAction *trayActPermaban_ = nullptr;
    WallpaperSetter wallpaperSetter_;
    RedditFetcher m_fetcher;
    CacheManager m_cache;
    ThumbnailViewer *thumbnailViewer_ = nullptr;
    QString currentSelectedPath_;
    QString currentWallpaperPath_;
    QStringList subscribedSubreddits_ = { "WidescreenWallpaper" };
    QPushButton *btnUpdate_ = nullptr;
    QPushButton *btnCleanup_ = nullptr;
    QSpinBox *updateCountSpin_ = nullptr;
    SourcesPanel *sourcesPanel_ = nullptr;
    FiltersPanel *filtersPanel_ = nullptr;
    // detail panel widgets
    QLabel *detailPath_ = nullptr;
    QLabel *detailSubreddit_ = nullptr;
    QLabel *detailResolution_ = nullptr;
    // score removed; use favorite flag instead
    QLabel *detailBanned_ = nullptr;
    bool m_initialLoadDone = false;
protected:
    void showEvent(QShowEvent *event) override;
};
