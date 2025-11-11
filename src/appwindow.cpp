#include "appwindow.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QAction>
#include <QPixmap>
#include <QPainter>
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
#include <QSaveFile>
#include <functional>
#include <QCheckBox>
#include <QGuiApplication>
#include <QScreen>
#include <QImageReader>
#include <QImage>
#include <QElapsedTimer>
#include <QShowEvent>
#include <QTimer>

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

// Render a single emoji into a QIcon for use in the tray. This keeps the icon
// visually consistent across desktops without adding resource files.
static QIcon createEmojiIcon(const QString &emoji, int size = 32) {
    QPixmap px(size, size);
    px.fill(Qt::transparent);
    QPainter p(&px);
    QFont f = QApplication::font();
    // Choose a reasonably large point size for the pixmap size.
    f.setPointSizeF(size * 0.6);
    p.setFont(f);
    p.setPen(Qt::black);
    p.drawText(px.rect(), Qt::AlignCenter, emoji);
    p.end();
    return QIcon(px);
}


AppWindow::AppWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Wallpaper C++");
    resize(900, 600);
    qDebug() << "AppWindow ctor: start";

    QVBoxLayout *l = new QVBoxLayout(this);
    qDebug() << "AppWindow ctor: created layout";
    QLabel *label = new QLabel("Subreddit: r/WidescreenWallpaper", this);
    qDebug() << "AppWindow ctor: created label";
    l->addWidget(label);
    QPushButton *btn = new QPushButton("🎲 New Random Wallpaper", this);
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
    // initialize allowed subreddits on the thumbnail viewer from current sources state
    // (will be applied after the viewer is created below)
    qDebug() << "AppWindow ctor: about to connect sourcesChanged";
    connect(sourcesPanel_, &SourcesPanel::sourcesChanged, this, [this, sourcesPathConfig](const QStringList &list){
        qDebug() << "AppWindow ctor: inside sourcesChanged lambda";
        subscribedSubreddits_ = list;
        // persist to config
        sourcesPanel_->saveToFile(sourcesPathConfig);
    });
    // when enabled (checked) list changes, tell the thumbnail viewer to update its allowed set
    connect(sourcesPanel_, &SourcesPanel::enabledSourcesChanged, this, [this, sourcesPathConfig](const QStringList &enabled){
        qDebug() << "AppWindow: enabledSourcesChanged:" << enabled;
        // persist the enabled/disabled state immediately so it survives restarts
        sourcesPanel_->saveToFile(sourcesPathConfig);
        thumbnailViewer_->setAllowedSubreddits(enabled);
        thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
    });
    qDebug() << "AppWindow ctor: connected sourcesChanged";

    // Place the Filters panel after the subreddit list (per user request).
    // We load the saved filter mode here, but defer wiring the mode-changed handler
    // until after the thumbnail viewer is created so the handler can update it safely.
    qDebug() << "AppWindow ctor: creating FiltersPanel (after SourcesPanel)";
    filtersPanel_ = new FiltersPanel(this);
    l->addWidget(filtersPanel_);
    // load persisted filter mode from config (if present)
    QDir().mkpath(configDir);
    QString configPath = configDir + "/config.json";
    QJsonObject cfg;
    QFile cfgf(configPath);
    if (cfgf.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(cfgf.readAll());
        if (doc.isObject()) cfg = doc.object();
        cfgf.close();
    }
    int savedMode = cfg.value("filter_mode").toInt((int)filtersPanel_->mode());
    filtersPanel_->setMode(static_cast<ThumbnailViewer::AspectFilterMode>(savedMode));

    qDebug() << "AppWindow ctor: before ThumbnailViewer";
    // thumbnail viewer
    thumbnailViewer_ = new ThumbnailViewer(this);
    qDebug() << "AppWindow ctor: created ThumbnailViewer";
    thumbnailViewer_->setMinimumHeight(300);
    // now apply the current enabled sources to the viewer
    thumbnailViewer_->setAllowedSubreddits(sourcesPanel_->enabledSources());
    l->addWidget(thumbnailViewer_, 1);
    qDebug() << "AppWindow ctor: added ThumbnailViewer to layout";
    // compute primary screen aspect ratio and set it on the thumbnail viewer
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize scrSize = screen ? screen->size() : QSize(1920,1080);
    double primaryAspect = double(scrSize.width()) / double(scrSize.height());
    thumbnailViewer_->setTargetAspectRatio(primaryAspect);
    // apply initial mode
    thumbnailViewer_->setAspectFilterMode(filtersPanel_->mode());
    // listen for mode changes and persist the selection
    connect(filtersPanel_, &FiltersPanel::modeChanged, this, [this, configPath](ThumbnailViewer::AspectFilterMode mode){
        thumbnailViewer_->setAspectFilterMode(mode);
        // persist selection atomically
        QJsonObject newCfg;
        QFile rcf(configPath);
        if (rcf.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(rcf.readAll());
            if (doc.isObject()) newCfg = doc.object();
            rcf.close();
        }
        newCfg["filter_mode"] = (int)mode;
        QSaveFile sf(configPath);
        if (sf.open(QIODevice::WriteOnly)) {
            sf.write(QJsonDocument(newCfg).toJson(QJsonDocument::Indented));
            sf.commit();
        } else {
            qWarning() << "Failed to write config file:" << configPath;
        }
        // reload thumbnails from cache so the filter takes effect immediately
        thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
    });
    // initial load of thumbnails deferred until the window is shown to allow correct layout
    connect(thumbnailViewer_, &ThumbnailViewer::imageSelected, this, &AppWindow::onThumbnailSelected);
    // double-click (activate) should set the wallpaper immediately
    connect(thumbnailViewer_, &ThumbnailViewer::imageActivated, this, [this](const QString &imagePath){
        qDebug() << "Thumbnail activated (double-click):" << imagePath;
        if (wallpaperSetter_.setWallpaper(imagePath)) {
            qDebug() << "Wallpaper set from thumbnail activation:" << imagePath;
            // record currently-set wallpaper
            currentWallpaperPath_ = imagePath;
            // Update tray actions enabled state now that we have a current wallpaper
            if (trayActThumbUp_) trayActThumbUp_->setEnabled(true);
            if (trayActThumbDown_) trayActThumbDown_->setEnabled(true);
            if (trayActPermaban_) trayActPermaban_->setEnabled(true);
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
    // Use a dice emoji as the tray icon
    trayIcon_ = new QSystemTrayIcon(createEmojiIcon(QString::fromUtf8("🎲")), this);
    QMenu *menu = new QMenu();
    QAction *actNew = new QAction("🎲 New Random Wallpaper", this);
    connect(actNew, &QAction::triggered, this, &AppWindow::onNewRandom);
    menu->addAction(actNew);
    // tray actions for quick votes/ban
    QAction *actThumbUp = new QAction("👍 Current Wallpaper", this);
    connect(actThumbUp, &QAction::triggered, this, &AppWindow::onThumbUp);
    actThumbUp->setEnabled(false);
    menu->addAction(actThumbUp);
    trayActThumbUp_ = actThumbUp;

    QAction *actThumbDown = new QAction("👎 Current Wallpaper", this);
    connect(actThumbDown, &QAction::triggered, this, &AppWindow::onThumbDown);
    actThumbDown->setEnabled(false);
    menu->addAction(actThumbDown);
    trayActThumbDown_ = actThumbDown;

    QAction *actPermaban = new QAction("💀 Perma-Ban Current Wallpaper", this);
    connect(actPermaban, &QAction::triggered, this, &AppWindow::onPermaban);
    actPermaban->setEnabled(false);
    menu->addAction(actPermaban);
    trayActPermaban_ = actPermaban;
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

void AppWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_initialLoadDone) {
        // Defer the initial load until after the first show/layout pass so
        // that the scroll viewport has a valid size and column calculation is correct.
        QTimer::singleShot(0, this, [this]() {
            thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
            // schedule a follow-up relayout after layouts settle to avoid a 1-column flash
            QTimer::singleShot(80, thumbnailViewer_, [this]() {
                thumbnailViewer_->relayoutGrid();
            });
        });
        m_initialLoadDone = true;
    }
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

    // ensure thumbnail viewer has up-to-date primary aspect (used by filters)
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize scrSize = screen ? screen->size() : QSize(1920,1080);
    double primaryAspect = double(scrSize.width()) / double(scrSize.height());
    thumbnailViewer_->setTargetAspectRatio(primaryAspect);

    struct Candidate { QString path; double weight; };
    QVector<Candidate> candidates;
    double totalWeight = 0.0;

    QElapsedTimer timer; timer.start();
    int scanned = 0;
    int considered = 0;

    for (const QFileInfo &fi : files) {
        QString path = fi.absoluteFilePath();
        scanned++;
        QString key = fi.fileName();
        QJsonObject entry = index.value(key).toObject();
        bool banned = entry.value("banned").toBool(false);
        if (banned) continue;

        // apply current filters via thumbnail viewer (centralized)
        if (!thumbnailViewer_->acceptsImage(path)) continue;
        considered++;

        int score = entry.value("score").toInt(0);
        // weight: base 1, plus positive score bonus (negative scores not helpful)
        double weight = 1.0 + std::max(0, score);
        candidates.append({ path, weight });
        totalWeight += weight;
    }

    if (candidates.empty()) {
        qWarning() << "No candidate wallpapers found in cache (after filters). scanned=" << scanned << "considered=" << considered << "ms=" << timer.elapsed();
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
    qDebug() << "onNewRandom: scanned=" << scanned << "considered=" << considered << "candidates=" << candidates.size() << "ms=" << timer.elapsed();
    if (wallpaperSetter_.setWallpaper(chosen)) {
        qDebug() << "Wallpaper set successfully from cache";
        // update UI/details for the chosen image
        onThumbnailSelected(chosen);
        // record currently-set wallpaper so tray actions operate on it
        currentWallpaperPath_ = chosen;
        if (trayActThumbUp_) trayActThumbUp_->setEnabled(true);
        if (trayActThumbDown_) trayActThumbDown_->setEnabled(true);
        if (trayActPermaban_) trayActPermaban_->setEnabled(true);
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

    // Enable/disable tray actions based on whether we have a current wallpaper
    bool hasCurrent = !currentWallpaperPath_.isEmpty();
    if (trayActThumbUp_) trayActThumbUp_->setEnabled(hasCurrent);
    if (trayActThumbDown_) trayActThumbDown_->setEnabled(hasCurrent);
    if (trayActPermaban_) trayActPermaban_->setEnabled(hasCurrent);
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
    QSaveFile sf(indexPath);
    if (!sf.open(QIODevice::WriteOnly)) return false;
    sf.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
    return sf.commit();
}

void AppWindow::onThumbUp() {
    // operate on the currently-set wallpaper
    if (currentWallpaperPath_.isEmpty()) return;
    QString key = QFileInfo(currentWallpaperPath_).fileName();
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
    // operate on the currently-set wallpaper
    if (currentWallpaperPath_.isEmpty()) return;
    QString key = QFileInfo(currentWallpaperPath_).fileName();
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
    // operate on the currently-set wallpaper
    if (currentWallpaperPath_.isEmpty()) return;
    QString key = QFileInfo(currentWallpaperPath_).fileName();
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
    QStringList allSources = sourcesPanel_ ? sourcesPanel_->enabledSources() : subscribedSubreddits_;
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
