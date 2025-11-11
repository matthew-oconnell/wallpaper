#include "filterspanel.h"
#include <QHBoxLayout>
#include <QLabel>

FiltersPanel::FiltersPanel(QWidget *parent)
    : QWidget(parent), combo_(new QComboBox(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    layout->addWidget(new QLabel("Aspect filter:", this));
    combo_->addItem("All", QVariant::fromValue<int>(ThumbnailViewer::FilterAll));
    combo_->addItem("Exact", QVariant::fromValue<int>(ThumbnailViewer::FilterExact));
    combo_->addItem("Rough", QVariant::fromValue<int>(ThumbnailViewer::FilterRough));
    combo_->setCurrentIndex(0);
    layout->addWidget(combo_);

    connect(combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){
        int v = combo_->currentData().toInt();
        emit modeChanged(static_cast<ThumbnailViewer::AspectFilterMode>(v));
    });
}

ThumbnailViewer::AspectFilterMode FiltersPanel::mode() const {
    int v = combo_->currentData().toInt();
    return static_cast<ThumbnailViewer::AspectFilterMode>(v);
}
