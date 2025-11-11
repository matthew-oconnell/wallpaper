#ifndef FILTERSPANEL_H
#define FILTERSPANEL_H

#include <QWidget>
#include <QComboBox>
#include "thumbnailviewer.h"

class FiltersPanel : public QWidget {
    Q_OBJECT
public:
    explicit FiltersPanel(QWidget *parent = nullptr);

    ThumbnailViewer::AspectFilterMode mode() const;
    void setMode(ThumbnailViewer::AspectFilterMode m);

signals:
    void modeChanged(ThumbnailViewer::AspectFilterMode newMode);

private:
    QComboBox *combo_;
};

#endif // FILTERSPANEL_H
