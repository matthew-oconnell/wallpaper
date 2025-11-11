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

signals:
    // Emitted when user clicks a thumbnail; path is full filesystem path to image
    void imageSelected(const QString &imagePath);

public slots:
    void refresh();

private:
    void clearGrid();
    void addThumbnail(const QString &filePath, int row, int col);

    QScrollArea *m_scroll;
    QWidget *m_container;
    QGridLayout *m_grid;
    QVector<ClickableLabel*> m_labels;
    int m_thumbSize = 200; // pixels
};

#endif // THUMBNAILVIEWER_H
