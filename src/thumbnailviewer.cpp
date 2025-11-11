#include "thumbnailviewer.h"
#include <QDir>
#include <QFileInfoList>
#include <QLabel>
#include <QImageReader>
#include <QPixmap>
#include <QMouseEvent>
#include <QVBoxLayout>

// Simple clickable QLabel
class ClickableLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setCursor(Qt::PointingHandCursor);
    }
signals:
    void clicked();
protected:
    void mouseReleaseEvent(QMouseEvent *ev) override {
        if (ev->button() == Qt::LeftButton) emit clicked();
        QLabel::mouseReleaseEvent(ev);
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
    if (pmLoaded.load(filePath)) {
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

    m_grid->addWidget(label, row, col);
    m_labels.append(label);
}

void ThumbnailViewer::loadFromCache(const QString &cacheDir)
{
    clearGrid();

    QDir dir(cacheDir);
    if (!dir.exists()) return;

    QStringList nameFilters;
    // common image extensions
    nameFilters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.webp" << "*.gif";
    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files, QDir::Time);

    const int columns = 4;
    int row = 0, col = 0;
    for (const QFileInfo &fi : files) {
        addThumbnail(fi.absoluteFilePath(), row, col);
        col++;
        if (col >= columns) { col = 0; row++; }
    }

    // adjust container layout
    m_container->adjustSize();
}

void ThumbnailViewer::refresh()
{
    // no-op; callers should call loadFromCache(cacheDir) when desired
}

#include "thumbnailviewer.moc"
