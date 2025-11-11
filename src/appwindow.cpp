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
#include "thumbnailviewer.h"
#include "sourcespanel.h"
#include <QLabel>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QThread>
#include <QStandardPaths>
#include <QDir>
#include <functional>

// Worker object to perform subreddit scanning & downloading in a background thread.
class UpdateWorker : public QObject {
    Q_OBJECT
public:
    explicit UpdateWorker(const QStringList &subs, QObject *parent = nullptr)
        : QObject(parent), subs_(subs) {}

public slots:
    void run() {
        for (const QString &sub : subs_) {
            RedditFetcher rf;
            std::vector<std::string> urls = rf.fetchRecentImageUrls(sub, 100);
            for (const auto &u : urls) {
                QString url = QString::fromStdString(u);
                CacheManager cache;
                QString cached = cache.downloadAndCache(url);
                if (!cached.isEmpty()) emit imageCached(cached, sub, url);
            }
        }
        emit finished();
    }

signals:
    void imageCached(const QString &cachedPath, const QString &subreddit, const QString &sourceUrl);
    void finished();

private:
    QStringList subs_;
};


AppWindow::AppWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Wallpaper C++");
    resize(900, 600);

    QVBoxLayout *l = new QVBoxLayout(this);
    QLabel *label = new QLabel("Subreddit: r/WidescreenWallpaper", this);
    l->addWidget(label);
    QPushButton *btn = new QPushButton("New Random Wallpaper", this);
    connect(btn, &QPushButton::clicked, this, &AppWindow::onNewRandom);
    l->addWidget(btn);

    QPushButton *btnUpdate = new QPushButton("Look For New Wallpapers", this);
    connect(btnUpdate, &QPushButton::clicked, this, &AppWindow::onUpdateCache);
    l->addWidget(btnUpdate);
    btnUpdate_ = btnUpdate;

    // Sources panel (manage subreddits)
    sourcesPanel_ = new SourcesPanel(this);
    // try to load persisted sources from config dir (~/.config/wallpaper/sources.json)
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/wallpaper";
    QDir().mkpath(configDir);
    QString sourcesPathConfig = configDir + "/sources.json";
    QString sourcesPathCache = m_cache.cacheDirPath() + "/sources.json"; // legacy location
    bool loaded = false;
    if (QFile::exists(sourcesPathConfig)) {
        loaded = sourcesPanel_->loadFromFile(sourcesPathConfig);
    }
    if (!loaded && QFile::exists(sourcesPathCache)) {
        // migrate from cache dir if present
        loaded = sourcesPanel_->loadFromFile(sourcesPathCache);
        if (loaded) {
            sourcesPanel_->saveToFile(sourcesPathConfig);
        }
    }
    if (!loaded) {
        // initialize with default
        sourcesPanel_->setSources(subscribedSubreddits_);
    } else {
        subscribedSubreddits_ = sourcesPanel_->sources();
    }
    l->addWidget(sourcesPanel_);
    connect(sourcesPanel_, &SourcesPanel::sourcesChanged, this, [this, sourcesPathConfig](const QStringList &list){
        subscribedSubreddits_ = list;
        // persist to config
        sourcesPanel_->saveToFile(sourcesPathConfig);
    });

    // thumbnail viewer
    thumbnailViewer_ = new ThumbnailViewer(this);
    thumbnailViewer_->setMinimumHeight(300);
    l->addWidget(thumbnailViewer_, 1);
    // load thumbnails from cache
    thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
    connect(thumbnailViewer_, &ThumbnailViewer::imageSelected, this, &AppWindow::onThumbnailSelected);
    // double-click (activate) should set the wallpaper immediately
    connect(thumbnailViewer_, &ThumbnailViewer::imageActivated, this, [this](const QString &imagePath){
        qDebug() << "Thumbnail activated (double-click):" << imagePath;
        if (wallpaperSetter_.setWallpaper(imagePath)) {
            qDebug() << "Wallpaper set from thumbnail activation:" << imagePath;
        } else {
            qWarning() << "Failed to set wallpaper for" << imagePath;
        }
    });

    // Details panel
    QWidget *detailWidget = new QWidget(this);
    QVBoxLayout *detailLayout = new QVBoxLayout(detailWidget);
    detailPath_ = new QLabel("Path: ", detailWidget);
    detailSubreddit_ = new QLabel("Subreddit: unknown", detailWidget);
    detailResolution_ = new QLabel("Resolution: ", detailWidget);
    detailScore_ = new QLabel("Score: 0", detailWidget);
    detailBanned_ = new QLabel("Banned: false", detailWidget);
    detailLayout->addWidget(detailPath_);
    detailLayout->addWidget(detailSubreddit_);
    detailLayout->addWidget(detailResolution_);
    detailLayout->addWidget(detailScore_);
    detailLayout->addWidget(detailBanned_);

    QHBoxLayout *actionsLayout = new QHBoxLayout();
    btnThumbUp_ = new QPushButton("👍", detailWidget);
    btnThumbDown_ = new QPushButton("👎", detailWidget);
    btnPermaban_ = new QPushButton("Perma-Ban", detailWidget);
    actionsLayout->addWidget(btnThumbUp_);
    actionsLayout->addWidget(btnThumbDown_);
    actionsLayout->addWidget(btnPermaban_);
    detailLayout->addLayout(actionsLayout);
    l->addWidget(detailWidget);

    connect(btnThumbUp_, &QPushButton::clicked, this, &AppWindow::onThumbUp);
    connect(btnThumbDown_, &QPushButton::clicked, this, &AppWindow::onThumbDown);
    connect(btnPermaban_, &QPushButton::clicked, this, &AppWindow::onPermaban);

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

void AppWindow::onThumbnailSelected(const QString &imagePath) {
    qDebug() << "Thumbnail selected:" << imagePath;
    currentSelectedPath_ = imagePath;
    detailPath_->setText(QString("Path: %1").arg(imagePath));

    // resolution
    QImage img(imagePath);
    if (!img.isNull()) {
        detailResolution_->setText(QString("Resolution: %1x%2").arg(img.width()).arg(img.height()));
    } else {
        detailResolution_->setText("Resolution: unknown");
    }

    // Load metadata index.json if present
    QString indexPath = m_cache.cacheDirPath() + "/index.json";
    QJsonObject rootObj;
    QFile f(indexPath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) rootObj = doc.object();
        f.close();
    }

    QString key = QFileInfo(imagePath).fileName();
    QJsonObject entry = rootObj.value(key).toObject();
    QString subreddit = entry.value("subreddit").toString("unknown");
    int score = entry.value("score").toInt(0);
    bool banned = entry.value("banned").toBool(false);

    detailSubreddit_->setText(QString("Subreddit: %1").arg(subreddit));
    detailScore_->setText(QString("Score: %1").arg(score));
    detailBanned_->setText(QString("Banned: %1").arg(banned ? "true" : "false"));
    
    // Optionally set wallpaper on click? We'll not auto-set; keep manual behavior.
}

static QJsonObject readIndex(const QString &indexPath) {
    QJsonObject rootObj;
    QFile f(indexPath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) rootObj = doc.object();
        f.close();
    }
    return rootObj;
}

static bool writeIndex(const QString &indexPath, const QJsonObject &rootObj) {
    QJsonDocument doc(rootObj);
    QFile f(indexPath);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

void AppWindow::onThumbUp() {
    if (currentSelectedPath_.isEmpty()) return;
    QString key = QFileInfo(currentSelectedPath_).fileName();
    QString indexPath = m_cache.cacheDirPath() + "/index.json";
    QJsonObject root = readIndex(indexPath);

    // config sources path
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/wallpaper";
    QDir().mkpath(configDir);
    QString sourcesPath = configDir + "/sources.json";

    // (no-op here)
    QJsonObject entry = root.value(key).toObject();
    int score = entry.value("score").toInt(0);
    score += 1;
    entry["score"] = score;
    root[key] = entry;
    if (writeIndex(indexPath, root)) {
        detailScore_->setText(QString("Score: %1").arg(score));
        qDebug() << "Updated score to" << score << "for" << key;
    } else {
        qWarning() << "Failed to write index.json";
    }
}

void AppWindow::onThumbDown() {
    if (currentSelectedPath_.isEmpty()) return;
    QString key = QFileInfo(currentSelectedPath_).fileName();
    QString indexPath = m_cache.cacheDirPath() + "/index.json";
    QJsonObject root = readIndex(indexPath);
    QJsonObject entry = root.value(key).toObject();
    int score = entry.value("score").toInt(0);
    score -= 1;
    entry["score"] = score;
    root[key] = entry;
    if (writeIndex(indexPath, root)) {
        detailScore_->setText(QString("Score: %1").arg(score));
        qDebug() << "Updated score to" << score << "for" << key;
    } else {
        qWarning() << "Failed to write index.json";
    }
}

void AppWindow::onPermaban() {
    if (currentSelectedPath_.isEmpty()) return;
    QString key = QFileInfo(currentSelectedPath_).fileName();
    QString indexPath = m_cache.cacheDirPath() + "/index.json";
    QJsonObject root = readIndex(indexPath);
    QJsonObject entry = root.value(key).toObject();
    entry["banned"] = true;
    root[key] = entry;
    if (writeIndex(indexPath, root)) {
        detailBanned_->setText("Banned: true");
        qDebug() << "Set perma-ban for" << key;
    } else {
        qWarning() << "Failed to write index.json";
    }
}

void AppWindow::onUpdateCache() {
    qDebug() << "Update cache: scanning subscribed subreddits...";
    QString indexPath = m_cache.cacheDirPath() + "/index.json";
    QJsonObject root = readIndex(indexPath);

    // Prepare ordered subreddit list: never-updated first, then oldest
    QStringList allSources = sourcesPanel_ ? sourcesPanel_->sources() : subscribedSubreddits_;
    QMap<QString,QDateTime> lastMap = sourcesPanel_ ? sourcesPanel_->lastUpdatedMap() : QMap<QString,QDateTime>();
    QStringList neverUpdated;
    QVector<QPair<QString,QDateTime>> updated;
    for (const QString &s : allSources) {
        if (lastMap.contains(s) && lastMap.value(s).isValid()) {
            updated.append(qMakePair(s, lastMap.value(s)));
        } else {
            neverUpdated.append(s);
        }
    }
    std::sort(updated.begin(), updated.end(), [](const QPair<QString,QDateTime> &a, const QPair<QString,QDateTime> &b){
        return a.second < b.second;
    });
    QStringList ordered;
    ordered += neverUpdated;
    for (const auto &p : updated) ordered.append(p.first);

    // Run update in background thread so UI stays responsive
    btnUpdate_->setEnabled(false);
    QThread *thread = new QThread;
    UpdateWorker *worker = new UpdateWorker(ordered);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &UpdateWorker::run);
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/wallpaper";
    QDir().mkpath(configDir);
    QString sourcesPath = configDir + "/sources.json";
    // When worker reports an image cached, update index.json and thumbnail viewer on UI thread
    connect(worker, &UpdateWorker::imageCached, this, [this, indexPath, sourcesPath](const QString &cached, const QString &subreddit, const QString &sourceUrl){
        // load index
        QJsonObject root = readIndex(indexPath);
        QString key = QFileInfo(cached).fileName();
        QJsonObject entry = root.value(key).toObject();
        entry["subreddit"] = subreddit;
        entry["downloaded_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        if (!entry.contains("score")) entry["score"] = 0;
        if (!entry.contains("banned")) entry["banned"] = false;
        entry["source_url"] = sourceUrl;
        root[key] = entry;
        writeIndex(indexPath, root);
        // add thumbnail to viewer incrementally
        thumbnailViewer_->addThumbnailFromPath(cached);
        // update last-updated for the subreddit and persist
        if (sourcesPanel_) {
            QDateTime now = QDateTime::currentDateTimeUtc();
            sourcesPanel_->setLastUpdated(subreddit, now);
            sourcesPanel_->saveToFile(sourcesPath);
        }
    });
    connect(worker, &UpdateWorker::finished, this, [this, thread, worker, indexPath](){
        qDebug() << "Background update finished";
        btnUpdate_->setEnabled(true);
        thread->quit();
        worker->deleteLater();
        thread->deleteLater();
    });
    thread->start();
}

#include "appwindow.moc"
