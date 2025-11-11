#include "thumbnailviewer.h"
#include <QDir>
#include <QFileInfoList>
#include <QLabel>
#include <QImageReader>
#include <QPixmap>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QImageReader>
#include <QDebug>
#include <QElapsedTimer>

// Simple clickable QLabel
class ClickableLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setCursor(Qt::PointingHandCursor);
    }
signals:
    void clicked();
    void doubleClicked();
protected:
    void mouseReleaseEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton) emit clicked();
        QLabel::mouseReleaseEvent(ev);
    }
    void mouseDoubleClickEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton) emit doubleClicked();
        QLabel::mouseDoubleClickEvent(ev);
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
    label->setScaledContents(false); // we will set a pixmap sized to preserve aspect ratio

    QPixmap pmLoaded;
    // Prefer a pre-generated thumbnail if present in the same directory
    QFileInfo fi(filePath);
    QString thumbCandidate = fi.absolutePath() + "/" + fi.baseName() + "-thumb.jpg";
    if (QFile::exists(thumbCandidate)) {
        if (!pmLoaded.load(thumbCandidate)) pmLoaded = QPixmap();
    }
    if (pmLoaded.isNull()) {
        if (!pmLoaded.load(filePath)) pmLoaded = QPixmap();
    }
    if (!pmLoaded.isNull()) {
        // Scale to fit within m_thumbSize x m_thumbSize while preserving aspect ratio
        QSize bound(m_thumbSize, m_thumbSize);
        QPixmap pm = pmLoaded.scaled(bound, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        label->setPixmap(pm);
        // Size the label to the actual scaled pixmap so the widget matches aspect ratio
        label->setFixedSize(pm.size());
    } else {
        // Fallback square placeholder
        label->setFixedSize(m_thumbSize, m_thumbSize);
        label->setText("?");
        label->setAlignment(Qt::AlignCenter);
    }

    connect(label, &ClickableLabel::clicked, this, [this, filePath](){
        emit imageSelected(filePath);
    });
    connect(label, &ClickableLabel::doubleClicked, this, [this, filePath](){
        emit imageActivated(filePath);
    });

    // store file path on the widget for dedupe checks
    label->setProperty("filePath", filePath);

    m_grid->addWidget(label, row, col);
    m_labels.append(label);
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

    QStringList nameFilters;
    // common image extensions
    nameFilters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.webp" << "*.gif";
    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files, QDir::Time);

    const int columns = 4;
    int row = 0, col = 0;
    for (const QFileInfo &fi : files) {
        scanned++;
        if (!acceptsImage(fi.absoluteFilePath())) {
            // skip filtered items without advancing the grid position so no empty slots are left
            continue;
        }
        accepted++;
        addThumbnail(fi.absoluteFilePath(), row, col);
        // advance grid position only when we actually added a thumbnail
        col++;
        if (col >= columns) { col = 0; row++; }
    }

    // adjust container layout
    m_container->adjustSize();
    qDebug() << "ThumbnailViewer::loadFromCache: scanned=" << scanned << "accepted=" << accepted << "thumbs=" << m_labels.size() << "ms=" << timer.elapsed();
}

void ThumbnailViewer::refresh()
{
    // no-op; callers should call loadFromCache(cacheDir) when desired
}

void ThumbnailViewer::addThumbnailFromPath(const QString &filePath)
{
    // compute next grid position
    int idx = m_labels.size();
    const int columns = 4;
    int row = idx / columns;
    int col = idx % columns;
    // avoid adding duplicates
    if (hasThumbnailForFile(filePath)) return;
    if (!acceptsImage(filePath)) return;
    addThumbnail(filePath, row, col);
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

bool ThumbnailViewer::acceptsImage(const QString &filePath) const
{
    // cheap quick-check: extension + existence
    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) return false;

    if (m_filterMode == FilterAll) return true;

    // consult in-memory index.json (if loaded) to avoid reading image files
    QString fname = fi.fileName();
    if (!m_indexJson.isEmpty() && m_indexJson.contains(fname)) {
        QJsonObject entry = m_indexJson.value(fname).toObject();
        if (entry.contains("width") && entry.contains("height")) {
            int w = entry.value("width").toInt(0);
            int h = entry.value("height").toInt(0);
            if (w <= 0 || h <= 0) return false;
            double ar = double(w) / double(h);
            if (m_filterMode == FilterExact) return qAbs(ar - m_targetAspect) <= 0.03;
            bool primaryHorizontal = m_targetAspect >= 1.0;
            bool imgHorizontal = w >= h;
            return primaryHorizontal == imgHorizontal;
        }
    }

    QImageReader r(filePath);
    QSize sz = r.size();
    if (sz.isEmpty()) {
        QImage img(filePath);
        if (img.isNull()) return false;
        sz = img.size();
    }
    if (sz.isEmpty()) return false;
    double ar = double(sz.width()) / double(sz.height());

    if (m_filterMode == FilterExact) {
        return qAbs(ar - m_targetAspect) <= 0.03;
    }
    // Rough: match orientation only (horizontal vs vertical)
    bool primaryHorizontal = m_targetAspect >= 1.0;
    bool imgHorizontal = sz.width() >= sz.height();
    return primaryHorizontal == imgHorizontal;
}

#include "thumbnailviewer.moc"
