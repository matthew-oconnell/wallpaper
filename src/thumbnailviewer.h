#ifndef THUMBNAILVIEWER_H
#define THUMBNAILVIEWER_H

#include <QWidget>
#include <QGridLayout>
#include <QScrollArea>
#include <QVector>
#include <QString>

class ClickableLabel;

class ThumbnailViewer : public QWidget {
    Q_OBJECT
public:
    explicit ThumbnailViewer(QWidget *parent = nullptr);

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

private:
    void clearGrid();
    void addThumbnail(const QString &filePath, int row, int col);

    QScrollArea *m_scroll;
    QWidget *m_container;
    QGridLayout *m_grid;
    QVector<ClickableLabel*> m_labels;
    int m_thumbSize = 200; // pixels
    bool m_filterAspect = false;
    double m_targetAspect = 16.0/9.0;
};

#endif // THUMBNAILVIEWER_H
