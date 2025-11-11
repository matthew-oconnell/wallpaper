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
#include <QCheckBox>
#include <QGuiApplication>
#include <QScreen>
#include <QImageReader>
#include <QImage>

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

// forward declarations for helpers defined later in this file
static QJsonObject readIndex(const QString &indexPath);
static bool writeIndex(const QString &indexPath, const QJsonObject &rootObj);


AppWindow::AppWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Wallpaper C++");
    resize(900, 600);
    qDebug() << "AppWindow ctor: start";

    QVBoxLayout *l = new QVBoxLayout(this);
    qDebug() << "AppWindow ctor: created layout";
    QLabel *label = new QLabel("Subreddit: r/WidescreenWallpaper", this);
    qDebug() << "AppWindow ctor: created label";
    l->addWidget(label);
    QPushButton *btn = new QPushButton("New Random Wallpaper", this);
    qDebug() << "AppWindow ctor: created New Random button";
    connect(btn, &QPushButton::clicked, this, &AppWindow::onNewRandom);
    l->addWidget(btn);

    QPushButton *btnUpdate = new QPushButton("Look For New Wallpapers", this);
    qDebug() << "AppWindow ctor: created Update button";
    connect(btnUpdate, &QPushButton::clicked, this, &AppWindow::onUpdateCache);
    l->addWidget(btnUpdate);
    btnUpdate_ = btnUpdate;

    qDebug() << "AppWindow ctor: before SourcesPanel";
    // Sources panel (manage subreddits)
    sourcesPanel_ = new SourcesPanel(this);
    qDebug() << "AppWindow ctor: created SourcesPanel";
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
    qDebug() << "AppWindow ctor: added SourcesPanel to layout";
    qDebug() << "AppWindow ctor: about to connect sourcesChanged";
    connect(sourcesPanel_, &SourcesPanel::sourcesChanged, this, [this, sourcesPathConfig](const QStringList &list){
        qDebug() << "AppWindow ctor: inside sourcesChanged lambda";
        subscribedSubreddits_ = list;
        // persist to config
        sourcesPanel_->saveToFile(sourcesPathConfig);
    });
    qDebug() << "AppWindow ctor: connected sourcesChanged";

    // Filtering panel will be created after the thumbnail viewer so we can set the target aspect safely.

    qDebug() << "AppWindow ctor: before ThumbnailViewer";
    // thumbnail viewer
    thumbnailViewer_ = new ThumbnailViewer(this);
    qDebug() << "AppWindow ctor: created ThumbnailViewer";
    thumbnailViewer_->setMinimumHeight(300);
    l->addWidget(thumbnailViewer_, 1);
    qDebug() << "AppWindow ctor: added ThumbnailViewer to layout";
    // Filtering panel: option to show only matching aspect ratio
    chkFilterAspect_ = new QCheckBox("Only show matching aspect ratio", this);
    l->addWidget(chkFilterAspect_);
    // compute primary screen aspect ratio and set it on the thumbnail viewer
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize scrSize = screen ? screen->size() : QSize(1920,1080);
    double primaryAspect = double(scrSize.width()) / double(scrSize.height());
    thumbnailViewer_->setTargetAspectRatio(primaryAspect);
    connect(chkFilterAspect_, &QCheckBox::toggled, this, [this](bool on){
        thumbnailViewer_->setFilterAspectRatioEnabled(on);
        // reload thumbnails from cache so the filter takes effect immediately
        thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
    });
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
    qDebug() << "AppWindow ctor: before Details panel";
    QWidget *detailWidget = new QWidget(this);
    qDebug() << "AppWindow ctor: created Details widget";
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
    qDebug() << "AppWindow ctor: connected detail buttons";

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
    qDebug() << "AppWindow ctor: tray icon shown, ctor end";
}

AppWindow::~AppWindow() {
}

void AppWindow::onNewRandom() {
    qDebug() << "Selecting a new random wallpaper from cache...";

    QString cacheDir = m_cache.cacheDirPath();
    QDir dir(cacheDir);
    if (!dir.exists()) {
        qWarning() << "Cache directory does not exist:" << cacheDir;
        return;
    }

    // Load index.json metadata
    QString indexPath = cacheDir + "/index.json";
    QJsonObject index = readIndex(indexPath);

    // Gather candidate images with weights
    QStringList nameFilters;
    nameFilters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.webp" << "*.gif";
    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files);

    // compute primary aspect if filter enabled
    bool filterAspect = chkFilterAspect_ ? chkFilterAspect_->isChecked() : false;
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize scrSize = screen ? screen->size() : QSize(1920,1080);
    double primaryAspect = double(scrSize.width()) / double(scrSize.height());

    struct Candidate { QString path; double weight; };
    QVector<Candidate> candidates;
    double totalWeight = 0.0;

    for (const QFileInfo &fi : files) {
        QString path = fi.absoluteFilePath();
        QString key = fi.fileName();
        QJsonObject entry = index.value(key).toObject();
        bool banned = entry.value("banned").toBool(false);
        if (banned) continue;

        // aspect filter
        if (filterAspect) {
            QImageReader r(path);
            QSize sz = r.size();
            if (sz.isEmpty()) {
                QImage img(path);
                if (img.isNull()) continue;
                sz = img.size();
            }
            double ar = double(sz.width()) / double(sz.height());
            if (qAbs(ar - primaryAspect) > 0.03) continue; // skip non-matching
        }

        int score = entry.value("score").toInt(0);
        // weight: base 1, plus positive score bonus (negative scores not helpful)
        double weight = 1.0 + std::max(0, score);
        candidates.append({ path, weight });
        totalWeight += weight;
    }

    if (candidates.empty()) {
        qWarning() << "No candidate wallpapers found in cache (after filters).";
        return;
    }

    // weighted random selection
    double r = QRandomGenerator::global()->generateDouble() * totalWeight;
    QString chosen;
    double acc = 0.0;
    for (const Candidate &c : candidates) {
        acc += c.weight;
        if (r <= acc) { chosen = c.path; break; }
    }
    if (chosen.isEmpty()) chosen = candidates.last().path;

    qDebug() << "Chosen wallpaper from cache:" << chosen;
    if (wallpaperSetter_.setWallpaper(chosen)) {
        qDebug() << "Wallpaper set successfully from cache";
        // update UI/details for the chosen image
        onThumbnailSelected(chosen);
    } else {
        qWarning() << "Failed to set wallpaper from cache:" << chosen;
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
