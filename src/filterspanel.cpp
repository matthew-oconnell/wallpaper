#include "filterspanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>

FiltersPanel::FiltersPanel(QWidget *parent)
    : QWidget(parent), combo_(new QComboBox(this))
{
    // vertical layout: aspect selector on first row, favorites checkbox on its own row
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);

    auto *h = new QHBoxLayout;
    h->addWidget(new QLabel("Aspect filter:", this));
    combo_->addItem("All", QVariant::fromValue<int>(ThumbnailViewer::FilterAll));
    combo_->addItem("Exact", QVariant::fromValue<int>(ThumbnailViewer::FilterExact));
    combo_->addItem("Rough", QVariant::fromValue<int>(ThumbnailViewer::FilterRough));
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
        emit modeChanged(static_cast<ThumbnailViewer::AspectFilterMode>(v));
    });
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
