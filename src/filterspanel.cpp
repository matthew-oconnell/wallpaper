#include "filterspanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QScrollArea>
#include <QGridLayout>

FiltersPanel::FiltersPanel(QWidget *parent)
    : QWidget(parent), combo_(new QComboBox(this))
{
    // vertical layout: aspect selector on first row, favorites checkbox on its own row
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);

    auto *h = new QHBoxLayout;
    h->addWidget(new QLabel("Filter:", this));
    combo_->addItem("All", QVariant::fromValue<int>(ThumbnailViewer::FilterAll));
    combo_->addItem("Exact", QVariant::fromValue<int>(ThumbnailViewer::FilterExact));
    combo_->addItem("Close Enough", QVariant::fromValue<int>(ThumbnailViewer::FilterRough));
    combo_->setCurrentIndex(0);
    h->addWidget(combo_);
    h->addStretch();
    layout->addLayout(h);

    // favorites-only on its own line
    favOnly_ = new QCheckBox("Favorites only", this);
    layout->addWidget(favOnly_);
    connect(favOnly_, &QCheckBox::toggled, this, [this](bool checked){ emit favoritesOnlyChanged(checked); });

    connect(combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){
        int v = combo_->currentData().toInt();
        auto mode = static_cast<ThumbnailViewer::AspectFilterMode>(v);
        emit modeChanged(mode);
        // Show/hide resolution checkboxes only when Exact mode is selected
        if (resolutionsContainer_) {
            resolutionsContainer_->setVisible(mode == ThumbnailViewer::FilterExact);
        }
    });
}

void FiltersPanel::setAvailableResolutions(const QList<QSize> &resolutions)
{
    // Create scrollable two-column grid the first time, or recreate the inner widget when updating
    if (!resolutionsContainer_) {
        resolutionsContainer_ = new QScrollArea(this);
        resolutionsContainer_->setWidgetResizable(true);
        resolutionsWidget_ = new QWidget;
        resolutionsGrid_ = new QGridLayout(resolutionsWidget_);
        resolutionsGrid_->setContentsMargins(0,0,0,0);
        resolutionsGrid_->setHorizontalSpacing(8);
        // header inside the scroll area (row 0 spans 2 columns)
        resolutionsGrid_->addWidget(new QLabel("Resolutions:" , resolutionsWidget_), 0, 0, 1, 2);
        resolutionsContainer_->setWidget(resolutionsWidget_);
        // give it a reasonable max height so a scrollbar appears when many entries exist
        resolutionsContainer_->setFixedHeight(200);
        this->layout()->addWidget(resolutionsContainer_);
        // set initial visibility according to current mode
        int v = combo_->currentData().toInt();
        auto mode = static_cast<ThumbnailViewer::AspectFilterMode>(v);
        resolutionsContainer_->setVisible(mode == ThumbnailViewer::FilterExact);
    } else {
        // rebuild the inner widget to clear previous entries
        if (resolutionsWidget_) {
            // delete old widget and create a fresh one
            delete resolutionsWidget_;
            resolutionsWidget_ = new QWidget;
            resolutionsGrid_ = new QGridLayout(resolutionsWidget_);
            resolutionsGrid_->setContentsMargins(0,0,0,0);
            resolutionsGrid_->setHorizontalSpacing(8);
            resolutionsGrid_->addWidget(new QLabel("Resolutions:", resolutionsWidget_), 0, 0, 1, 2);
            resolutionsContainer_->setWidget(resolutionsWidget_);
        }
    }

    // Clear previous map
    m_resolutionChecks.clear();

    // Create checkboxes for each resolution; default to checked. Place them in two columns starting at row 1.
    int idx = 0;
    int row = 1;
    for (const QSize &s : resolutions) {
        QString key = QString("%1x%2").arg(s.width()).arg(s.height());
        QCheckBox *cb = new QCheckBox(key, resolutionsWidget_);
        cb->setChecked(true);
        int col = idx % 2;
        row = 1 + (idx / 2);
        resolutionsGrid_->addWidget(cb, row, col);
        m_resolutionChecks.insert(key, cb);
        connect(cb, &QCheckBox::toggled, this, [this]() {
            // gather selected
            QList<QSize> sel;
            for (auto it = m_resolutionChecks.constBegin(); it != m_resolutionChecks.constEnd(); ++it) {
                if (it.value()->isChecked()) {
                    QString k = it.key();
                    QStringList parts = k.split('x');
                    if (parts.size() == 2) {
                        int w = parts[0].toInt();
                        int h = parts[1].toInt();
                        sel.append(QSize(w,h));
                    }
                }
            }
            emit resolutionsChanged(sel);
        });
        ++idx;
    }
}

ThumbnailViewer::AspectFilterMode FiltersPanel::mode() const {
    int v = combo_->currentData().toInt();
    return static_cast<ThumbnailViewer::AspectFilterMode>(v);
}

void FiltersPanel::setMode(ThumbnailViewer::AspectFilterMode m) {
    // find the index with matching data and set it
    for (int i = 0; i < combo_->count(); ++i) {
        if (combo_->itemData(i).toInt() == static_cast<int>(m)) {
            combo_->setCurrentIndex(i);
            return;
        }
    }
}

bool FiltersPanel::favoritesOnly() const {
    return favOnly_ ? favOnly_->isChecked() : false;
}

void FiltersPanel::setFavoritesOnly(bool v) {
    if (!favOnly_) return;
    favOnly_->setChecked(v);
}
