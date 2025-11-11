#include "appwindow.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QAction>
#include <QPixmap>
#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QTimer>
#include <QRandomGenerator>
#include "redditfetcher.h"
#include "cachemanager.h"
#include "thumbnailviewer.h"
#include "sourcespanel.h"
#include "updateworker.h"
#include <QFrame>
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
#include <QSaveFile>
#include <QDir>
#include <QTimer>
#include <QScreen>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QMessageBox>
#include <QFont>
#include <QRunnable>
#include <QThreadPool>
#include <QSet>
#include <QMetaObject>

// forward declarations for helper functions defined later in this file
static QJsonObject readIndex(const QString &indexPath);
static bool writeIndex(const QString &indexPath, const QJsonObject &rootObj);

// CleanupTask: deletes cached images whose subreddit is not in the allowed set
class CleanupTask : public QRunnable {
public:
    CleanupTask(const QString &cacheDir, const QSet<QString> &allowed, QObject *main)
        : m_cacheDir(cacheDir), m_allowed(allowed), m_main(main) {}
    void run() override {
        QString indexPath = QDir(m_cacheDir).filePath("index.json");
        QJsonObject root = readIndex(indexPath);
        QStringList toRemove;
        for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
            QString key = it.key();
            QJsonObject entry = it.value().toObject();
            QString sub = entry.value("subreddit").toString();
            if (!sub.isEmpty() && !m_allowed.contains(sub)) {
                toRemove << key;
            }
        }
        for (const QString &k : toRemove) {
            QJsonObject entry = root.value(k).toObject();
            QString filepath = QDir(m_cacheDir).filePath(k);
            QFile::remove(filepath);
            QString thumb = entry.value("thumbnail").toString();
            if (!thumb.isEmpty()) QFile::remove(QDir(m_cacheDir).filePath(thumb));
            root.remove(k);
        }
        writeIndex(indexPath, root);

        // refresh UI on main thread
        if (m_main) {
            QMetaObject::invokeMethod(m_main, "cleanupFinished", Qt::QueuedConnection);
        }
    }
private:
    QString m_cacheDir;
    QSet<QString> m_allowed;
    QObject *m_main;
};

void AppWindow::startCleanup()
{
    if (!btnCleanup_) return;
    btnCleanup_->setEnabled(false);
    QString cacheDir = m_cache.cacheDirPath();
    QStringList allowed;
    if (sourcesPanel_) allowed = sourcesPanel_->sources();
    else allowed = subscribedSubreddits_;
    QSet<QString> allowedSet;
    for (const QString &s : allowed) allowedSet.insert(s);
    QThreadPool::globalInstance()->start(new CleanupTask(cacheDir, allowedSet, this));
}

void AppWindow::cleanupFinished()
{
    // reload thumbnails and counts, re-enable cleanup button
    thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
    if (sourcesPanel_) sourcesPanel_->updateCounts(m_cache.cacheDirPath());
    if (btnCleanup_) btnCleanup_->setEnabled(true);
}

AppWindow::AppWindow(QWidget *parent)
    : QWidget(parent)
{
    qDebug() << "AppWindow ctor: start";
    // default window size requested by user
    this->resize(900, 600);
    // ensure config dir path is available for later use
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/wallaroo";

    // top-level layout: left sidebar (sources + filters) and right main area
    QHBoxLayout *main = new QHBoxLayout(this);

    // left sidebar panel (narrow)
    QWidget *leftPanel = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(4,4,4,4);

    // sources panel (left side) — created early so other init can refer to it
    sourcesPanel_ = new SourcesPanel(this);
    // try to load persisted sources from config (if present)
    QString sourcesPath = configDir + "/sources.json";
    sourcesPanel_->loadFromFile(sourcesPath);
    leftLayout->addWidget(sourcesPanel_);
    connect(sourcesPanel_, &SourcesPanel::enabledSourcesChanged, this, [this](const QStringList &enabled){
        if (!thumbnailViewer_) return;
        // update allowed subreddits and reload thumbnails so the filter takes effect immediately
        thumbnailViewer_->setAllowedSubreddits(enabled);
        thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
        // update counts displayed in the sources panel
        if (sourcesPanel_) sourcesPanel_->updateCounts(m_cache.cacheDirPath());
    });

    // persist any changes to the sources list
    connect(sourcesPanel_, &SourcesPanel::sourcesChanged, this, [this, sourcesPath](const QStringList &){
        if (!sourcesPanel_->saveToFile(sourcesPath)) {
            qWarning() << "Failed to save sources file:" << sourcesPath;
        }
    });

    // handle per-subreddit update requests from the sources panel context menu
    connect(sourcesPanel_, &SourcesPanel::updateRequested, this, &AppWindow::onUpdateSubredditRequested);

    // small helper to render an emoji into a tray icon (fallback)
    auto createEmojiIcon = [](const QString &emoji)->QIcon{
        QPixmap pix(32,32);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        QFont f = p.font(); f.setPointSize(18);
        p.setFont(f);
        p.drawText(pix.rect(), Qt::AlignCenter, emoji);
        return QIcon(pix);
    };

    

    // Place the Filters panel after the subreddit list (per user request).
    // We load the saved filter mode here, but defer wiring the mode-changed handler
    // until after the thumbnail viewer is created so the handler can update it safely.
    qDebug() << "AppWindow ctor: creating FiltersPanel (after SourcesPanel)";
    filtersPanel_ = new FiltersPanel(this);
    leftLayout->addWidget(filtersPanel_);
    leftLayout->addStretch();
    leftPanel->setMinimumWidth(260);
    main->addWidget(leftPanel);

    // right main panel
    QWidget *rightPanel = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
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
    bool savedFavOnly = cfg.value("favorites_only").toBool(false);
    filtersPanel_->setFavoritesOnly(savedFavOnly);

    qDebug() << "AppWindow ctor: before ThumbnailViewer";
    // thumbnail viewer
    thumbnailViewer_ = new ThumbnailViewer(this);
    qDebug() << "AppWindow ctor: created ThumbnailViewer";
    thumbnailViewer_->setMinimumHeight(300);
    // now apply the current enabled sources to the viewer
    thumbnailViewer_->setAllowedSubreddits(sourcesPanel_->enabledSources());
    rightLayout->addWidget(thumbnailViewer_, 1);
    qDebug() << "AppWindow ctor: added ThumbnailViewer to right panel";
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
        if (filtersPanel_) filtersPanel_->setAvailableResolutions(thumbnailViewer_->availableResolutions());
        if (sourcesPanel_) sourcesPanel_->updateCounts(m_cache.cacheDirPath());
    });
    connect(filtersPanel_, &FiltersPanel::favoritesOnlyChanged, this, [this, configPath](bool favOnly){
        thumbnailViewer_->setFavoritesOnly(favOnly);
        // persist selection atomically
        QJsonObject newCfg;
        QFile rcf(configPath);
        if (rcf.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(rcf.readAll());
            if (doc.isObject()) newCfg = doc.object();
            rcf.close();
        }
        newCfg["favorites_only"] = favOnly;
        QSaveFile sf(configPath);
        if (sf.open(QIODevice::WriteOnly)) {
            sf.write(QJsonDocument(newCfg).toJson(QJsonDocument::Indented));
            sf.commit();
        } else {
            qWarning() << "Failed to write config file:" << configPath;
        }
        thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
        if (filtersPanel_) filtersPanel_->setAvailableResolutions(thumbnailViewer_->availableResolutions());
        if (sourcesPanel_) sourcesPanel_->updateCounts(m_cache.cacheDirPath());
    });
    
    // Manual update and cleanup controls (restore deleted control):
    btnUpdate_ = new QPushButton("Update Library", this);
    btnUpdate_->setToolTip("Fetch new images and update the cache/index");
    btnCleanup_ = new QPushButton("Cleanup Library", this);
    btnCleanup_->setToolTip("Remove images leftover from deleted subreddits");
    // place buttons on one row (will be added to right panel)
    QHBoxLayout *updateRow = new QHBoxLayout();
    updateRow->addWidget(btnUpdate_);
    updateRow->addWidget(btnCleanup_);
    connect(btnUpdate_, &QPushButton::clicked, this, &AppWindow::onUpdateCache);
    connect(btnCleanup_, &QPushButton::clicked, this, &AppWindow::startCleanup);
    // add the update/cleanup row into the left sidebar so controls are together
    // first create the auto-random control: "Select a new random wallpaper every [spin] [unit]"
    QHBoxLayout *autoRow = new QHBoxLayout();
    autoRow->addWidget(new QLabel("Select a new random wallpaper every", this));
    autoIntervalSpin_ = new QSpinBox(this);
    autoIntervalSpin_->setRange(0, 1000000);
    // load saved auto-interval from config (default to 15 minutes)
    int savedAuto = cfg.value("auto_interval").toInt(15);
    QString savedUnit = cfg.value("auto_unit").toString("minutes");
    autoIntervalSpin_->setValue(savedAuto);
    autoIntervalSpin_->setToolTip("Interval value (0 to disable)");
    autoRow->addWidget(autoIntervalSpin_);
    autoIntervalUnit_ = new QComboBox(this);
    autoIntervalUnit_->addItem("seconds");
    autoIntervalUnit_->addItem("minutes");
    autoIntervalUnit_->addItem("hours");
    autoIntervalUnit_->addItem("on restart");
    // map saved unit string to combo index
    int unitIndex = 3;
    if (savedUnit == "seconds") unitIndex = 0;
    else if (savedUnit == "minutes") unitIndex = 1;
    else if (savedUnit == "hours") unitIndex = 2;
    else unitIndex = 3;
    autoIntervalUnit_->setCurrentIndex(unitIndex);
    autoRow->addWidget(autoIntervalUnit_);
    leftLayout->addLayout(autoRow);

    leftLayout->addLayout(updateRow);

    // timer for automatic random wallpaper selection
    autoTimer_ = new QTimer(this);
    autoTimer_->setSingleShot(false);
    connect(autoTimer_, &QTimer::timeout, this, &AppWindow::onNewRandom);

    // helper to apply and persist timer whenever controls change
    auto applyAutoSettings = [this, configPath]() {
        if (!autoIntervalSpin_ || !autoIntervalUnit_ || !autoTimer_) return;
        int val = autoIntervalSpin_->value();
        QString unit = autoIntervalUnit_->currentText();
        if (unit == "on restart" || val <= 0) {
            if (autoTimer_->isActive()) autoTimer_->stop();
        } else {
            qint64 msec = val;
            if (unit == "seconds") msec = msec * 1000LL;
            else if (unit == "minutes") msec = msec * 60LL * 1000LL;
            else if (unit == "hours") msec = msec * 60LL * 60LL * 1000LL;
            if (msec > INT_MAX) msec = INT_MAX;
            int imsec = static_cast<int>(msec);
            if (autoTimer_->interval() != imsec || !autoTimer_->isActive()) {
                autoTimer_->stop();
                autoTimer_->start(imsec);
            }
        }
        // persist selection atomically
        QJsonObject newCfg;
        QFile rcf(configPath);
        if (rcf.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(rcf.readAll());
            if (doc.isObject()) newCfg = doc.object();
            rcf.close();
        }
        newCfg["auto_interval"] = autoIntervalSpin_->value();
        newCfg["auto_unit"] = autoIntervalUnit_->currentText();
        QSaveFile sf(configPath);
        if (sf.open(QIODevice::WriteOnly)) {
            sf.write(QJsonDocument(newCfg).toJson(QJsonDocument::Indented));
            sf.commit();
        } else {
            qWarning() << "Failed to write config file:" << configPath;
        }
    };
    connect(autoIntervalSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, applyAutoSettings);
    connect(autoIntervalUnit_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, applyAutoSettings);
    // apply once at startup to use saved/default values
    applyAutoSettings();
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
                if (trayActFavorite_) trayActFavorite_->setEnabled(true);
                if (trayActPermaban_) trayActPermaban_->setEnabled(true);
        } else {
            QString err = wallpaperSetter_.lastError();
            QMessageBox::warning(this, "Set wallpaper failed", QString("Failed to set wallpaper %1\n%2").arg(imagePath).arg(err));
            qWarning() << "Failed to set wallpaper for" << imagePath << ";" << err;
        }
    });

    // connect context-menu actions from thumbnail viewer
    connect(thumbnailViewer_, &ThumbnailViewer::favoriteRequested, this, &AppWindow::onThumbnailFavoriteRequested);
    connect(thumbnailViewer_, &ThumbnailViewer::permabanRequested, this, &AppWindow::onThumbnailPermabanRequested);

    // Wire FiltersPanel resolution selection -> ThumbnailViewer selected resolutions
    if (filtersPanel_) {
        connect(filtersPanel_, &FiltersPanel::resolutionsChanged, this, [this](const QList<QSize> &sel){
            if (thumbnailViewer_) thumbnailViewer_->setSelectedResolutions(sel);
        });
    }

    // Details panel
    qDebug() << "AppWindow ctor: before Details panel";
    QWidget *detailWidget = new QWidget(this);
    qDebug() << "AppWindow ctor: created Details widget";
    QVBoxLayout *detailLayout = new QVBoxLayout(detailWidget);
    // path / subreddit / resolution details
    detailPath_ = new QLabel("Path: ", detailWidget);
    detailSubreddit_ = new QLabel("Subreddit: unknown", detailWidget);
    detailResolution_ = new QLabel("Resolution: ", detailWidget);
    detailLayout->addWidget(detailPath_);
    detailLayout->addWidget(detailSubreddit_);
    detailLayout->addWidget(detailResolution_);

    // no inline favorite / permaban buttons in detail panel; actions are available from thumbnail context menu
    rightLayout->addWidget(detailWidget);
    qDebug() << "AppWindow ctor: connected detail panel (no inline action buttons)";

    // insert a vertical divider between left and right panels for visual clarity
    QFrame *divider = new QFrame(this);
    divider->setFrameShape(QFrame::VLine);
    divider->setFrameShadow(QFrame::Sunken);
    divider->setLineWidth(1);
    divider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    main->addWidget(divider);

    // finish assembling main layout: add rightPanel to the main HBox
    main->addWidget(rightPanel, 1);

    // tray
    // Use a dice emoji as the tray icon
    trayIcon_ = new QSystemTrayIcon(createEmojiIcon(QString::fromUtf8("🎲")), this);
    QMenu *menu = new QMenu();
    // Order: Set Random, Random Favorite, Open, Favorite, Ban, Quit
    QAction *actNew = new QAction("🎲 Set Random", this);
    connect(actNew, &QAction::triggered, this, &AppWindow::onNewRandom);
    menu->addAction(actNew);

    // There's no single standard "random+heart" emoji, use a small combo that reads well:
    // "🔀❤️" (shuffle + heart) looks like "random favorite". You can change to "🎲❤️" if you prefer dice+heart.
    QAction *actRandFav = new QAction("🎲❤️ Random Favorite", this);
    connect(actRandFav, &QAction::triggered, this, &AppWindow::onRandomFavorite);
    menu->addAction(actRandFav);
    trayActRandomFavorite_ = actRandFav;

    QAction *actOpen = new QAction("Open", this);
    connect(actOpen, &QAction::triggered, this, [this]() {
        // Restore and activate the main window
        this->show();
        this->raise();
        this->activateWindow();
        // If minimized, restore
        this->setWindowState((this->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    });
    menu->addAction(actOpen);

    // Favorite (operates on current wallpaper)
    QAction *actFavorite = new QAction("❤️ Favorite", this);
    connect(actFavorite, &QAction::triggered, this, &AppWindow::onToggleFavorite);
    actFavorite->setEnabled(false);
    menu->addAction(actFavorite);
    trayActFavorite_ = actFavorite;

    // Ban (short label)
    QAction *actPermaban = new QAction("💀 Ban", this);
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
    // Handle tray activation:
    // - Single left click (Trigger) -> open window
    // - Right click (Context) -> context menu (provided by QSystemTrayIcon)
    connect(trayIcon_, &QSystemTrayIcon::activated, this, &AppWindow::onTrayActivated);
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
            if (filtersPanel_) filtersPanel_->setAvailableResolutions(thumbnailViewer_->availableResolutions());
            if (filtersPanel_) filtersPanel_->setAvailableResolutions(thumbnailViewer_->availableResolutions());
            if (filtersPanel_) filtersPanel_->setAvailableResolutions(thumbnailViewer_->availableResolutions());
            if (sourcesPanel_) sourcesPanel_->updateCounts(m_cache.cacheDirPath());
            // schedule a follow-up relayout after layouts settle to avoid a 1-column flash
            QTimer::singleShot(80, thumbnailViewer_, [this]() {
                thumbnailViewer_->relayoutGrid();
            });
        });
        m_initialLoadDone = true;
    }
}

void AppWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        // Single left click: select a new random wallpaper
        qDebug() << "Tray icon clicked (Trigger): selecting new random wallpaper";
        onNewRandom();
        return;
    }
    // Context (right-click) is handled by QSystemTrayIcon by showing the menu
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

    // Gather candidate images by scanning all files and accepting common
    // image extensions — allow filenames that include query-strings (e.g.
    // "file.jpg?width=...") which glob-based filters miss.
    QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoSymLinks);
    QRegularExpression extRegex(R"(\.(?:png|jpe?g|bmp|webp|gif)(?:$|\?))", QRegularExpression::CaseInsensitiveOption);

    // ensure thumbnail viewer has up-to-date primary aspect (used by filters)
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize scrSize = screen ? screen->size() : QSize(1920,1080);
    double primaryAspect = double(scrSize.width()) / double(scrSize.height());
    thumbnailViewer_->setTargetAspectRatio(primaryAspect);

    struct Candidate { QString path; };
    QVector<Candidate> candidates;

    QElapsedTimer timer; timer.start();
    int scanned = 0;
    int considered = 0;

    for (const QFileInfo &fi : files) {
        // skip non-image files (and accept filenames that have a trailing
        // query-string after the extension)
        if (!extRegex.match(fi.fileName()).hasMatch()) continue;
        QString path = fi.absoluteFilePath();
        scanned++;
        QString key = fi.fileName();
        QJsonObject entry = index.value(key).toObject();
        bool banned = entry.value("banned").toBool(false);
        if (banned) continue;

        // apply current filters via thumbnail viewer (centralized)
        if (!thumbnailViewer_->acceptsImage(path)) continue;
        considered++;

        // uniform selection: just add candidate
        candidates.append({ path });
    }

    if (candidates.empty()) {
        qWarning() << "No candidate wallpapers found in cache (after filters). scanned=" << scanned << "considered=" << considered << "ms=" << timer.elapsed();
        return;
    }

    // uniform random selection
    int idx = QRandomGenerator::global()->bounded(candidates.size());
    QString chosen = candidates[idx].path;

    qDebug() << "Chosen wallpaper from cache:" << chosen;
    qDebug() << "onNewRandom: scanned=" << scanned << "considered=" << considered << "candidates=" << candidates.size() << "ms=" << timer.elapsed();
    if (wallpaperSetter_.setWallpaper(chosen)) {
        qDebug() << "Wallpaper set successfully from cache";
        // update UI/details for the chosen image
        onThumbnailSelected(chosen);
    // record currently-set wallpaper so tray actions operate on it
    currentWallpaperPath_ = chosen;
    if (trayActFavorite_) trayActFavorite_->setEnabled(true);
    if (trayActPermaban_) trayActPermaban_->setEnabled(true);
    } else {
        QString err = wallpaperSetter_.lastError();
        QMessageBox::warning(this, "Set wallpaper failed", QString("Failed to set wallpaper %1\n%2").arg(chosen).arg(err));
        qWarning() << "Failed to set wallpaper from cache:" << chosen << ";" << err;
    }
}

void AppWindow::onRandomFavorite() {
    qDebug() << "Selecting a random favorited wallpaper from cache...";
    QString cacheDir = m_cache.cacheDirPath();
    QDir dir(cacheDir);
    if (!dir.exists()) {
        qWarning() << "Cache directory does not exist:" << cacheDir;
        return;
    }
    QString indexPath = cacheDir + "/index.json";
    QJsonObject index = readIndex(indexPath);

    struct Candidate { QString path; };
    QVector<Candidate> candidates;

    QFileInfoList filesFav = dir.entryInfoList(QDir::Files | QDir::NoSymLinks);
    QRegularExpression extRegexFav(R"(\.(?:png|jpe?g|bmp|webp|gif)(?:$|\?))", QRegularExpression::CaseInsensitiveOption);

    for (const QFileInfo &fi : filesFav) {
        if (!extRegexFav.match(fi.fileName()).hasMatch()) continue;
        QString path = fi.absoluteFilePath();
        QString key = fi.fileName();
        QJsonObject entry = index.value(key).toObject();
        bool banned = entry.value("banned").toBool(false);
        bool fav = entry.value("favorite").toBool(false);
        if (banned || !fav) continue;
        // respect filters
        if (!thumbnailViewer_->acceptsImage(path)) continue;
        candidates.append({ path });
    }

    if (candidates.empty()) {
        qWarning() << "No favorited wallpapers found (after filters). Falling back to random.";
        // fall back to plain random wallpaper
        onNewRandom();
        return;
    }

    int idx = QRandomGenerator::global()->bounded(candidates.size());
    QString chosen = candidates[idx].path;
    qDebug() << "Chosen favorite wallpaper:" << chosen;
    if (wallpaperSetter_.setWallpaper(chosen)) {
        qDebug() << "Wallpaper set successfully from favorite";
        onThumbnailSelected(chosen);
        currentWallpaperPath_ = chosen;
        if (trayActFavorite_) trayActFavorite_->setEnabled(true);
        if (trayActPermaban_) trayActPermaban_->setEnabled(true);
    } else {
        qWarning() << "Failed to set favorite wallpaper:" << chosen;
    }
}

void AppWindow::onThumbnailSelected(const QString &imagePath) {
    qDebug() << "Thumbnail selected:" << imagePath;
    currentSelectedPath_ = imagePath;
    // Avoid letting extremely long file paths expand the window when
    // thumbnails are selected programmatically (e.g. auto-rotation).
    // Elide the displayed path in the label and keep the full path in
    // the tooltip so users can still copy it if needed.
    QFontMetrics fm(detailPath_->font());
    int maxPixels = qMax(200, this->width() / 3); // heuristic width to elide to
    QString elided = fm.elidedText(imagePath, Qt::ElideMiddle, maxPixels);
    detailPath_->setText(QString("Path: %1").arg(elided));
    detailPath_->setToolTip(imagePath);

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
    bool banned = entry.value("banned").toBool(false);

    detailSubreddit_->setText(QString("Subreddit: %1").arg(subreddit));
    
    // Optionally set wallpaper on click? We'll not auto-set; keep manual behavior.

    // Favorite state for this thumbnail (displayed via filters; no inline button)
    bool fav = entry.value("favorite").toBool(false);
    Q_UNUSED(fav);

    // Enable/disable tray favorite/permaban based on whether we have a current wallpaper
    bool hasCurrent = !currentWallpaperPath_.isEmpty();
    if (trayActFavorite_) trayActFavorite_->setEnabled(hasCurrent);
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

// removed old thumb-up/thumb-down handlers; favorites replace scoring

void AppWindow::onToggleFavorite() {
    // toggle favorite for currently selected thumbnail if possible
    QString targetPath = currentSelectedPath_.isEmpty() ? currentWallpaperPath_ : currentSelectedPath_;
    if (targetPath.isEmpty()) return;
    QString key = QFileInfo(targetPath).fileName();
    QString indexPath = m_cache.cacheDirPath() + "/index.json";
    QJsonObject root = readIndex(indexPath);
    QJsonObject entry = root.value(key).toObject();
    bool fav = entry.value("favorite").toBool(false);
    fav = !fav;
    entry["favorite"] = fav;
    root[key] = entry;
    if (writeIndex(indexPath, root)) {
        // refresh thumbnails to reflect favorite state
        if (thumbnailViewer_) thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
        qDebug() << "Set favorite=" << fav << "for" << key;
    } else {
        qWarning() << "Failed to write index.json";
    }
}

void AppWindow::onThumbnailFavoriteRequested(const QString &imagePath)
{
    if (imagePath.isEmpty()) return;
    QString key = QFileInfo(imagePath).fileName();
    QString indexPath = m_cache.cacheDirPath() + "/index.json";
    QJsonObject root = readIndex(indexPath);
    QJsonObject entry = root.value(key).toObject();
    bool fav = entry.value("favorite").toBool(false);
    fav = !fav;
    entry["favorite"] = fav;
    root[key] = entry;
    if (writeIndex(indexPath, root)) {
        qDebug() << "Context-favorite set=" << fav << "for" << key;
        if (thumbnailViewer_) thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
    } else {
        qWarning() << "Failed to write index.json";
    }
}

void AppWindow::onThumbnailPermabanRequested(const QString &imagePath)
{
    if (imagePath.isEmpty()) return;
    QString key = QFileInfo(imagePath).fileName();
    QString indexPath = m_cache.cacheDirPath() + "/index.json";
    QJsonObject root = readIndex(indexPath);
    QJsonObject entry = root.value(key).toObject();
    entry["banned"] = true;
    root[key] = entry;
    if (writeIndex(indexPath, root)) {
        qDebug() << "Context-permaban set for" << key;
        if (thumbnailViewer_) thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
        // After permabanning, pick a new favorite wallpaper if the permabanned one is current
        if (!currentWallpaperPath_.isEmpty() && QFileInfo(currentWallpaperPath_).fileName() == key) {
            QTimer::singleShot(0, this, [this]() { this->onRandomFavorite(); });
        }
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
        qDebug() << "Set perma-ban for" << key;
        // After permabanning the current wallpaper, immediately load a random favorited wallpaper
        QTimer::singleShot(0, this, [this]() { this->onRandomFavorite(); });
    } else {
        qWarning() << "Failed to write index.json";
    }
}

void AppWindow::onUpdateCache() {
    // Start a background UpdateWorker that fetches recent posts and downloads images.
    if (!btnUpdate_) return;
    btnUpdate_->setEnabled(false);
    btnUpdate_->setText("Updating...");

    // determine enabled subreddits
    QStringList subs;
    if (sourcesPanel_) subs = sourcesPanel_->enabledSources();
    if (subs.isEmpty()) subs = subscribedSubreddits_; // fallback

    // create worker + thread
    UpdateWorker *worker = new UpdateWorker(&m_fetcher, &m_cache, subs);
    QThread *t = new QThread(this);
    worker->moveToThread(t);

    // propagate imageCached -> update url_map.json and index.json
    connect(worker, &UpdateWorker::imageCached, this, [this](const QString &localPath, const QString &subreddit, const QString &sourceUrl){
        // update url_map.json in config dir
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/wallaroo";
        QDir().mkpath(configDir);
        QString urlMapPath = configDir + "/url_map.json";
        QJsonObject urlmap;
        QFile f(urlMapPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) urlmap = doc.object();
            f.close();
        }
        QUrl u(sourceUrl);
        u.setQuery(QString());
        u.setFragment(QString());
        QString norm = u.toString();
        QJsonArray arr = urlmap.value(norm).toArray();
        bool found = false;
        for (const QJsonValue &v : arr) if (v.toString() == subreddit) { found = true; break; }
        if (!found) arr.append(subreddit);
        urlmap.insert(norm, arr);
        QSaveFile sf(urlMapPath);
        if (sf.open(QIODevice::WriteOnly)) {
            sf.write(QJsonDocument(urlmap).toJson(QJsonDocument::Indented));
            sf.commit();
        }

        // update index.json to set subreddit if missing
        QString indexPath = m_cache.cacheDirPath() + "/index.json";
        QJsonObject root = readIndex(indexPath);
        QString key = QFileInfo(localPath).fileName();
        QJsonObject entry = root.value(key).toObject();
        if (entry.value("subreddit").toString().isEmpty()) {
            entry["subreddit"] = subreddit;
            root[key] = entry;
            writeIndex(indexPath, root);
        }
    });

    // update per-subreddit progress UI
    if (sourcesPanel_) {
        connect(worker, &UpdateWorker::started, sourcesPanel_, &SourcesPanel::startUpdateProgress);
        connect(worker, &UpdateWorker::finishedSubreddit, sourcesPanel_, &SourcesPanel::finishUpdateProgress);
    }

    connect(worker, &UpdateWorker::error, this, [this](const QString &msg){
        qWarning() << "UpdateWorker error:" << msg;
    });

    connect(worker, &UpdateWorker::finished, this, [this, t, worker](){
        if (btnUpdate_) {
            btnUpdate_->setEnabled(true);
            btnUpdate_->setText("Update");
        }
        // refresh thumbnails and counts
        if (thumbnailViewer_) thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
        if (filtersPanel_) filtersPanel_->setAvailableResolutions(thumbnailViewer_->availableResolutions());
        if (sourcesPanel_) sourcesPanel_->updateCounts(m_cache.cacheDirPath());
        // cleanup
        t->quit();
        worker->deleteLater();
        t->deleteLater();
    });

    connect(t, &QThread::started, worker, &UpdateWorker::start);
    // ensure thread stops if app exits
    connect(qApp, &QCoreApplication::aboutToQuit, t, &QThread::quit);
    t->start();
}

void AppWindow::onUpdateSubredditRequested(const QString &subreddit, int perSubLimit)
{
    if (subreddit.isEmpty()) return;
    if (!btnUpdate_) return;
    btnUpdate_->setEnabled(false);
    btnUpdate_->setText(QString("Updating %1...").arg(subreddit));

    QStringList subs; subs << subreddit;
    UpdateWorker *worker = new UpdateWorker(&m_fetcher, &m_cache, subs, perSubLimit);
    QThread *t = new QThread(this);
    worker->moveToThread(t);

    connect(worker, &UpdateWorker::imageCached, this, [this](const QString &localPath, const QString &sub, const QString &sourceUrl){
        // same handling as in onUpdateCache
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/wallaroo";
        QDir().mkpath(configDir);
        QString urlMapPath = configDir + "/url_map.json";
        QJsonObject urlmap;
        QFile f(urlMapPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            if (doc.isObject()) urlmap = doc.object();
            f.close();
        }
        QUrl u(sourceUrl);
        u.setQuery(QString()); u.setFragment(QString());
        QString norm = u.toString();
        QJsonArray arr = urlmap.value(norm).toArray();
        bool found = false; for (const QJsonValue &v : arr) if (v.toString() == sub) { found = true; break; }
        if (!found) arr.append(sub);
        urlmap.insert(norm, arr);
        QSaveFile sf(urlMapPath);
        if (sf.open(QIODevice::WriteOnly)) { sf.write(QJsonDocument(urlmap).toJson(QJsonDocument::Indented)); sf.commit(); }

        QString indexPath = m_cache.cacheDirPath() + "/index.json";
        QJsonObject root = readIndex(indexPath);
        QString key = QFileInfo(localPath).fileName();
        QJsonObject entry = root.value(key).toObject();
        if (entry.value("subreddit").toString().isEmpty()) { entry["subreddit"] = sub; root[key] = entry; writeIndex(indexPath, root); }
    });

    // per-subreddit progress connections
    if (sourcesPanel_) {
        connect(worker, &UpdateWorker::started, sourcesPanel_, &SourcesPanel::startUpdateProgress);
        connect(worker, &UpdateWorker::finishedSubreddit, sourcesPanel_, &SourcesPanel::finishUpdateProgress);
    }

    connect(worker, &UpdateWorker::finished, this, [this, t, worker]() {
        if (btnUpdate_) { btnUpdate_->setEnabled(true); btnUpdate_->setText("Update Library"); }
        if (thumbnailViewer_) thumbnailViewer_->loadFromCache(m_cache.cacheDirPath());
        if (filtersPanel_) filtersPanel_->setAvailableResolutions(thumbnailViewer_->availableResolutions());
        if (sourcesPanel_) sourcesPanel_->updateCounts(m_cache.cacheDirPath());
        t->quit(); worker->deleteLater(); t->deleteLater();
    });

    connect(t, &QThread::started, worker, &UpdateWorker::start);
    connect(qApp, &QCoreApplication::aboutToQuit, t, &QThread::quit);
    t->start();
}

#include "appwindow.moc"
