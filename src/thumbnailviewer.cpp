#include "thumbnailviewer.h"
#include <QDir>
#include <QFileInfoList>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QImageReader>
#include <QPixmap>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QImageReader>
#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QThreadPool>
#include <QRunnable>
#include <QSaveFile>

// Simple clickable QLabel
class ClickableLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setCursor(Qt::PointingHandCursor);
        setContextMenuPolicy(Qt::DefaultContextMenu);
    }
signals:
    void clicked();
    void doubleClicked();
    void contextRequested(const QPoint &pos);
protected:
    void mouseReleaseEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton) emit clicked();
        QLabel::mouseReleaseEvent(ev);
    }
    void mouseDoubleClickEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton) emit doubleClicked();
        QLabel::mouseDoubleClickEvent(ev);
    }
    void contextMenuEvent(QContextMenuEvent *ev) override {
        emit contextRequested(ev->pos());
        QLabel::contextMenuEvent(ev);
    }
};

ThumbnailViewer::ThumbnailViewer(QWidget *parent)
    : QWidget(parent),
      m_scroll(new QScrollArea(this)),
      m_container(new QWidget),
      m_grid(new QGridLayout)
{
    m_container->setLayout(m_grid);
    m_scroll->setWidget(m_container);
    m_scroll->setWidgetResizable(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(m_scroll);
    setLayout(layout);

    qRegisterMetaType<QImage>("QImage");
}

void ThumbnailViewer::clearGrid()
{
    for (ClickableLabel *l : m_labels) {
        m_grid->removeWidget(l);
        l->deleteLater();
    }
    m_labels.clear();
}

void ThumbnailViewer::addThumbnail(const QString &filePath, int row, int col)
{
    auto *label = new ClickableLabel;
    label->setAlignment(Qt::AlignCenter);
    label->setScaledContents(false); // pixmap will be set when ready

    // Initial placeholder: fixed square so layout is stable
    label->setFixedSize(m_thumbSize, m_thumbSize);
    label->setText("...");
    label->setAlignment(Qt::AlignCenter);

    connect(label, &ClickableLabel::clicked, this, [this, filePath](){
        emit imageSelected(filePath);
    });
    connect(label, &ClickableLabel::doubleClicked, this, [this, filePath](){
        emit imageActivated(filePath);
    });

    // right-click context menu for per-thumbnail actions (favorite / perma-ban)
    connect(label, &ClickableLabel::contextRequested, this, [this, label, filePath](const QPoint &pt){
        QMenu menu(label);
        QAction *actFav = menu.addAction(QString::fromUtf8("♥ Favorite"));
        QAction *actBan = menu.addAction(QString::fromUtf8("💀 Perma-Ban"));
        QAction *chosen = menu.exec(label->mapToGlobal(pt));
        if (chosen == actFav) emit favoriteRequested(filePath);
        else if (chosen == actBan) emit permabanRequested(filePath);
    });

    // store file path on the widget for dedupe checks
    label->setProperty("filePath", filePath);

    m_grid->addWidget(label, row, col);
    m_labels.append(label);

    // Asynchronously load the thumbnail/image in a background runnable to avoid blocking UI
    QString path = filePath;
    ThumbnailViewer *self = this;
    class LoadRunnable : public QRunnable {
    public:
        LoadRunnable(const QString &p, ThumbnailViewer *v, int sz) : p(p), viewer(v), thumbSz(sz) {}
        void run() override {
            QImage img;
            QFileInfo fi(p);
            QString thumbCandidate = fi.absolutePath() + "/" + fi.baseName() + "-thumb.jpg";
            if (QFile::exists(thumbCandidate)) {
                QImageReader r(thumbCandidate);
                img = r.read();
            }
            if (img.isNull()) {
                QImageReader r2(p);
                img = r2.read();
            }
            if (img.isNull()) return;
            QImage scaled = img.scaled(thumbSz, thumbSz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            // invoke the UI thread to set the pixmap using the functor overload (no metatype required)
            // copy members into local variables so the lambda can capture them by value
            ThumbnailViewer *v = viewer;
            QString pathCopy = p;
            QImage imgCopy = scaled;
            QMetaObject::invokeMethod(v, [v, pathCopy, imgCopy]() {
                v->onThumbnailLoaded(pathCopy, imgCopy);
            }, Qt::QueuedConnection);
        }
    private:
        QString p;
        ThumbnailViewer *viewer;
        int thumbSz;
    };
    QThreadPool::globalInstance()->start(new LoadRunnable(path, self, m_thumbSize));
}

void ThumbnailViewer::loadFromCache(const QString &cacheDir)
{
    clearGrid();

    QElapsedTimer timer; timer.start();
    int scanned = 0;
    int accepted = 0;

    QDir dir(cacheDir);
    if (!dir.exists()) return;

    // load index.json once for metadata lookups
    m_indexPath = dir.filePath("index.json");
    m_indexJson = QJsonObject();
    QFile idxfile(m_indexPath);
    if (idxfile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(idxfile.readAll());
        if (doc.isObject()) m_indexJson = doc.object();
        idxfile.close();
    }

    // Reset selected resolutions when loading a new cache; caller (FiltersPanel) will be updated
    m_selectedResolutions.clear();

    // Build the list of files to consider. Prefer entries from index.json for
    // fast metadata lookups (avoids opening/decoding many images). If index.json
    // is empty, fall back to scanning the directory.
    QVector<QFileInfo> fileList;
    if (!m_indexJson.isEmpty()) {
        // Iterate index keys and only include files that exist on disk
        const QJsonObject::Iterator itEnd = m_indexJson.end();
        QVector<QFileInfo> missingMeta;
        for (QJsonObject::Iterator it = m_indexJson.begin(); it != itEnd; ++it) {
            QString key = it.key();
            QString path = dir.filePath(key);
            QFileInfo fi(path);
            if (!fi.exists() || !fi.isFile()) continue;
            QJsonObject entry = it.value().toObject();
            if (entry.contains("width") && entry.contains("height")) {
                fileList.append(fi);
            } else {
                // Defer files missing metadata to a background task and skip them for now
                missingMeta.append(fi);
            }
        }
        // Schedule background tasks to generate metadata/thumbnails for missing entries
        if (!missingMeta.isEmpty()) {
            QString dirPath = dir.absolutePath();
            for (const QFileInfo &mfi : missingMeta) {
                QString mpath = mfi.absoluteFilePath();
                QString mkey = mfi.fileName();
                // lightweight QRunnable to compute size and thumbnail and update index.json
                class EnsureMetaRunnable : public QRunnable {
                public:
                    EnsureMetaRunnable(const QString &filePath, const QString &key, const QString &dirPath)
                        : filePath(filePath), key(key), dirPath(dirPath) {}
                    void run() override {
                        QImageReader r(filePath);
                        QSize sz = r.size();
                        QImage img;
                        if (sz.isEmpty()) {
                            img = QImage(filePath);
                            if (!img.isNull()) sz = img.size();
                        }
                        QString thumbName;
                        if (!img.isNull()) {
                            QByteArray hash = QFileInfo(filePath).baseName().toUtf8();
                            thumbName = QString::fromUtf8(hash) + "-thumb.jpg";
                            QString thumbPath = QDir(dirPath).filePath(thumbName);
                            QImage thumb = img.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                            thumb.save(thumbPath, "JPEG", 85);
                        }
                        QString indexPath = QDir(dirPath).filePath("index.json");
                        QJsonObject rootObj;
                        QFile idxf(indexPath);
                        if (idxf.open(QIODevice::ReadOnly)) {
                            QJsonDocument doc = QJsonDocument::fromJson(idxf.readAll());
                            if (doc.isObject()) rootObj = doc.object();
                            idxf.close();
                        }
                        QJsonObject entry = rootObj.value(key).toObject();
                        if (!sz.isEmpty()) { entry["width"] = sz.width(); entry["height"] = sz.height(); }
                        if (!thumbName.isEmpty()) entry["thumbnail"] = thumbName;
                        rootObj[key] = entry;
                        QSaveFile sf(indexPath);
                        if (sf.open(QIODevice::WriteOnly)) {
                            sf.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
                            sf.commit();
                        }
                    }
                private:
                    QString filePath;
                    QString key;
                    QString dirPath;
                };
                QThreadPool::globalInstance()->start(new EnsureMetaRunnable(mpath, mkey, dirPath));
            }
        }
        // Sort by modification time (newest first) to show recent items first
        std::sort(fileList.begin(), fileList.end(), [](const QFileInfo &a, const QFileInfo &b){
            return a.lastModified() > b.lastModified();
        });
    } else {
        QStringList nameFilters;
        // common image extensions
        nameFilters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.webp" << "*.gif";
        QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files, QDir::Time);
        for (const QFileInfo &fi : files) fileList.append(fi);
    }

    const int columns = computeColumns();
    int row = 0, col = 0;
    for (const QFileInfo &fi : fileList) {
        scanned++;
        // Use acceptsImage which will consult m_indexJson (fast) where possible.
        if (!acceptsImage(fi.absoluteFilePath())) continue;
        accepted++;
        addThumbnail(fi.absoluteFilePath(), row, col);
        // advance grid position only when we actually added a thumbnail
        col++;
        if (col >= columns) { col = 0; row++; }
    }

    // After populating, ensure the widgets are laid out according to current viewport width
    relayoutGrid();
    qDebug() << "ThumbnailViewer::loadFromCache: scanned=" << scanned << "accepted=" << accepted << "thumbs=" << m_labels.size() << "ms=" << timer.elapsed();
}

QList<QSize> ThumbnailViewer::availableResolutions() const
{
    QSet<QSize> set;
    if (m_indexJson.isEmpty()) return {};
    for (auto it = m_indexJson.constBegin(); it != m_indexJson.constEnd(); ++it) {
        QJsonObject entry = it.value().toObject();
        if (entry.contains("width") && entry.contains("height")) {
            int w = entry.value("width").toInt(0);
            int h = entry.value("height").toInt(0);
            if (w > 0 && h > 0) set.insert(QSize(w,h));
        }
    }
    QList<QSize> out = set.values();
    std::sort(out.begin(), out.end(), [](const QSize &a, const QSize &b){
        if (a.width() != b.width()) return a.width() < b.width();
        return a.height() < b.height();
    });
    return out;
}

void ThumbnailViewer::setSelectedResolutions(const QList<QSize> &resolutions)
{
    m_selectedResolutions = resolutions;
    // caller should call loadFromCache or refresh to apply filter; we'll call refresh()
    refresh();
}

void ThumbnailViewer::refresh()
{
    // no-op; callers should call loadFromCache(cacheDir) when desired
}

void ThumbnailViewer::addThumbnailFromPath(const QString &filePath)
{
    // compute next grid position
    int idx = m_labels.size();
    const int columns = computeColumns();
    int row = idx / columns;
    int col = idx % columns;
    // avoid adding duplicates
    if (hasThumbnailForFile(filePath)) return;
    if (!acceptsImage(filePath)) return;
    addThumbnail(filePath, row, col);
}

int ThumbnailViewer::computeColumns() const
{
    // compute how many columns fit in the scroll viewport given thumb size and spacing
    int viewportWidth = m_scroll->viewport()->width();
    // If the viewport hasn't been laid out yet, try fallbacks to get a reasonable width
    if (viewportWidth <= 0) viewportWidth = m_scroll->width();
    if (viewportWidth <= 0) {
        QWidget *p = parentWidget();
        if (p) viewportWidth = p->width();
    }
    if (viewportWidth <= 0) {
        QScreen *screen = QGuiApplication::primaryScreen();
        viewportWidth = screen ? screen->size().width() : 1024;
    }
    int hSpacing = m_grid->horizontalSpacing();
    if (hSpacing < 0) hSpacing = 0;
    int cell = m_thumbSize + hSpacing;
    if (cell <= 0) return 1;
    int cols = viewportWidth / cell;
    if (cols < 1) cols = 1;
    return cols;
}

void ThumbnailViewer::relayoutGrid()
{
    const int columns = computeColumns();
    if (columns <= 1) {
        // still ensure widgets are in first column sequentially
    }
    // Remove and re-add widgets in their original order
    int idx = 0;
    for (ClickableLabel *l : m_labels) {
        m_grid->removeWidget(l);
        int row = idx / columns;
        int col = idx % columns;
        m_grid->addWidget(l, row, col);
        idx++;
    }
    m_container->updateGeometry();
}

bool ThumbnailViewer::hasThumbnailForFile(const QString &filePath) const
{
    QString targetName = QFileInfo(filePath).fileName();
    for (ClickableLabel *l : m_labels) {
        QVariant p = l->property("filePath");
        if (!p.isValid()) continue;
        QString existing = p.toString();
        if (QFileInfo(existing).fileName() == targetName) return true;
    }
    return false;
}

void ThumbnailViewer::onThumbnailLoaded(const QString &filePath, const QImage &img)
{
    // find the label for this filePath and set the pixmap
    for (ClickableLabel *l : m_labels) {
        QVariant p = l->property("filePath");
        if (!p.isValid()) continue;
        QString existing = p.toString();
        if (existing == filePath) {
            QPixmap pm = QPixmap::fromImage(img);
            // keep the label's fixed size (m_thumbSize) so the grid cell size is stable
            QSize target = QSize(m_thumbSize, m_thumbSize);
            QPixmap pmScaled = pm.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            l->setPixmap(pmScaled);
            l->setText("");
            break;
        }
    }
}

void ThumbnailViewer::setFilterAspectRatioEnabled(bool enabled)
{
    if (enabled) setAspectFilterMode(FilterExact);
    else setAspectFilterMode(FilterAll);
}

void ThumbnailViewer::setTargetAspectRatio(double ratio)
{
    if (ratio <= 0.0) return;
    m_targetAspect = ratio;
}

void ThumbnailViewer::setAspectFilterMode(AspectFilterMode mode)
{
    m_filterMode = mode;
}

ThumbnailViewer::AspectFilterMode ThumbnailViewer::aspectFilterMode() const
{
    return m_filterMode;
}

void ThumbnailViewer::setAllowedSubreddits(const QStringList &allowed)
{
    m_allowedSubreddits.clear();
    for (const QString &s : allowed) {
        QString n = s.trimmed();
        if (n.startsWith("r/", Qt::CaseInsensitive)) n = n.mid(2);
        n = n.toLower();
        if (!n.isEmpty()) m_allowedSubreddits.append(n);
    }
}

void ThumbnailViewer::setFavoritesOnly(bool v)
{
    // Caller should call loadFromCache to refresh view after changing this
    // but we store the flag so acceptsImage can consult it.
    // We'll store it as a runtime-only flag.
    // Add a dynamic property on this object so we don't need a header change for storage here.
    this->setProperty("favoritesOnly", v);
}

bool ThumbnailViewer::favoritesOnly() const
{
    QVariant v = this->property("favoritesOnly");
    return v.isValid() ? v.toBool() : false;
}

void ThumbnailViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Re-layout thumbnails to fit new width
    relayoutGrid();
}

bool ThumbnailViewer::acceptsImage(const QString &filePath) const
{
    // cheap quick-check: extension + existence
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) return false;

    // enforce allowed-subreddits first (so subreddit filtering applies regardless of aspect filter)
    QString fname = fi.fileName();
    if (!m_indexJson.isEmpty() && m_indexJson.contains(fname)) {
        QJsonObject entry = m_indexJson.value(fname).toObject();
        if (!m_allowedSubreddits.isEmpty()) {
            QString sr = entry.value("subreddit").toString().trimmed();
            if (sr.startsWith("r/", Qt::CaseInsensitive)) sr = sr.mid(2);
            sr = sr.toLower();
            if (sr.isEmpty() || !m_allowedSubreddits.contains(sr)) {
                qDebug() << "ThumbnailViewer: rejecting" << fname << "because subreddit" << sr << "not allowed";
                return false;
            }
        }
        // Respect per-image 'banned' flag in index.json: don't show banned photos
        if (entry.value("banned").toBool(false)) {
            qDebug() << "ThumbnailViewer: rejecting" << fname << "because banned flag is set";
            return false;
        }
    } else {
        // If we don't have metadata and an allowlist is configured, reject
        if (!m_allowedSubreddits.isEmpty()) {
            qDebug() << "ThumbnailViewer: rejecting" << fname << "because missing subreddit metadata while allowlist active";
            return false;
        }
    }

    // If favorites-only filter is enabled, reject non-favorited images
    if (favoritesOnly()) {
        if (!m_indexJson.isEmpty() && m_indexJson.contains(fname)) {
            QJsonObject entry = m_indexJson.value(fname).toObject();
            bool fav = entry.value("favorite").toBool(false);
            if (!fav) {
                qDebug() << "ThumbnailViewer: rejecting" << fname << "because not favorited";
                return false;
            }
        } else {
            // No metadata -> can't know favorite status -> reject
            qDebug() << "ThumbnailViewer: rejecting" << fname << "because missing metadata for favorites filter";
            return false;
        }
    }

    // If aspect filtering is disabled, accept (we have already enforced subreddit allowlist above)
    if (m_filterMode == FilterAll) return true;

    // consult in-memory index.json for size if available to avoid decoding image
    int w = 0, h = 0;
    if (!m_indexJson.isEmpty() && m_indexJson.contains(fname)) {
        QJsonObject entry = m_indexJson.value(fname).toObject();
        if (entry.contains("width") && entry.contains("height")) {
            w = entry.value("width").toInt(0);
            h = entry.value("height").toInt(0);
        }
    }
    QSize sz;
    if (w > 0 && h > 0) {
        sz = QSize(w,h);
    } else {
        QImageReader r(filePath);
        sz = r.size();
        if (sz.isEmpty()) {
            QImage img(filePath);
            if (img.isNull()) return false;
            sz = img.size();
        }
    }
    if (sz.isEmpty()) return false;
    double ar = double(sz.width()) / double(sz.height());

    // (If we reached here we either have metadata or no allowlist; proceed to aspect checks.)

    if (m_filterMode == FilterExact) {
        // If selected resolutions are specified, only accept images that match one of them
        if (!m_selectedResolutions.isEmpty()) {
            for (const QSize &s : m_selectedResolutions) {
                if (s.width() == sz.width() && s.height() == sz.height()) return true;
            }
            return false;
        }
        // Fallback: previous behavior (aspect-based exact match) if no resolutions selected
        return qAbs(ar - m_targetAspect) <= 0.03;
    }
    // Rough: match orientation only (horizontal vs vertical)
    bool primaryHorizontal = m_targetAspect >= 1.0;
    bool imgHorizontal = sz.width() >= sz.height();
    if (primaryHorizontal != imgHorizontal) return false;

    // Now ensure that when center-cropped to the target aspect ratio the resulting
    // dimensions are at least as large as the current primary screen dimensions.
    QScreen *screen = QGuiApplication::primaryScreen();
    QSize scrSize = screen ? screen->size() : QSize(1920,1080);
    int screenW = scrSize.width();
    int screenH = scrSize.height();

    int cropW, cropH;
    if (ar > m_targetAspect) {
        // image is wider than target -> crop width
        cropH = sz.height();
        cropW = int(double(cropH) * m_targetAspect + 0.5);
    } else {
        // image is taller (or equal) -> crop height
        cropW = sz.width();
        cropH = int(double(cropW) / m_targetAspect + 0.5);
    }
    return (cropW >= screenW) && (cropH >= screenH);
}

#include "thumbnailviewer.moc"
