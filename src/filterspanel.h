#ifndef FILTERSPANEL_H
#define FILTERSPANEL_H

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include "thumbnailviewer.h"

class FiltersPanel : public QWidget {
    Q_OBJECT
public:
    explicit FiltersPanel(QWidget *parent = nullptr);

    ThumbnailViewer::AspectFilterMode mode() const;
    void setMode(ThumbnailViewer::AspectFilterMode m);
    bool favoritesOnly() const;
    void setFavoritesOnly(bool v);

signals:
    void modeChanged(ThumbnailViewer::AspectFilterMode newMode);
    void favoritesOnlyChanged(bool newValue);

private:
    QComboBox *combo_;
    QCheckBox *favOnly_ = nullptr;
};

#endif // FILTERSPANEL_H
