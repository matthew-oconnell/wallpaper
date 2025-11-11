#ifndef THUMBNAILVIEWER_H
#define THUMBNAILVIEWER_H

#include <QWidget>
#include <QGridLayout>
#include <QScrollArea>
#include <QVector>
#include <QString>
#include <QJsonObject>

class ClickableLabel;

class ThumbnailViewer : public QWidget {
    Q_OBJECT
public:
    explicit ThumbnailViewer(QWidget *parent = nullptr);

    enum AspectFilterMode {
        FilterAll = 0,
        FilterExact = 1,
        FilterRough = 2
    };

    // Load thumbnails from cache directory (e.g. ~/.cache/wallpaper)
    void loadFromCache(const QString &cacheDir);
    
public slots:
    // Add a single thumbnail from a file path (used for incremental updates)
    void addThumbnailFromPath(const QString &filePath);

signals:
    // Emitted when user clicks a thumbnail; path is full filesystem path to image
    void imageSelected(const QString &imagePath);
    // Emitted when user double-clicks a thumbnail (activate)
    void imageActivated(const QString &imagePath);

public slots:
    void refresh();

    // Return true if a thumbnail for the given file path (or filename) already exists in the view
    bool hasThumbnailForFile(const QString &filePath) const;

    // Aspect-ratio filtering: when enabled, only show thumbnails that match the target aspect ratio
    void setFilterAspectRatioEnabled(bool enabled);
    void setTargetAspectRatio(double ratio); // width/height
    void setAspectFilterMode(AspectFilterMode mode);
    AspectFilterMode aspectFilterMode() const;

    // Return true if the thumbnail viewer would accept (render/select) this image given current filters
    bool acceptsImage(const QString &filePath) const;

private slots:
    void onThumbnailLoaded(const QString &filePath, const QImage &img);
private:
    void clearGrid();
    void addThumbnail(const QString &filePath, int row, int col);

    QScrollArea *m_scroll;
    QWidget *m_container;
    QGridLayout *m_grid;
    QVector<ClickableLabel*> m_labels;
    int m_thumbSize = 200; // pixels
    AspectFilterMode m_filterMode = FilterAll;
    double m_targetAspect = 16.0/9.0;
    // cached index.json for the current cache dir (loaded by loadFromCache)
    QJsonObject m_indexJson;
    QString m_indexPath;
};

#endif // THUMBNAILVIEWER_H
