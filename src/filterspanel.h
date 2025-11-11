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
    // Populate the panel with available resolutions (width x height) and show checkboxes
    void setAvailableResolutions(const QList<QSize> &resolutions);

signals:
    void modeChanged(ThumbnailViewer::AspectFilterMode newMode);
    void favoritesOnlyChanged(bool newValue);
    // Emitted when the user changes the selection of resolutions (checkboxes)
    void resolutionsChanged(const QList<QSize> &selected);

private:
    QComboBox *combo_;
    QCheckBox *favOnly_ = nullptr;
    // Container for dynamically generated resolution checkboxes (scrollable, 2-column grid)
    QScrollArea *resolutionsContainer_ = nullptr;
    QWidget *resolutionsWidget_ = nullptr;
    QGridLayout *resolutionsGrid_ = nullptr;
    QMap<QString, QCheckBox*> m_resolutionChecks;
};

#endif // FILTERSPANEL_H
