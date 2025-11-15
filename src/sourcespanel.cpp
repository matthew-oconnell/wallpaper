#include "sourcespanel.h"

#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QLabel>
#include <QProgressBar>
#include <QHBoxLayout>

SourcesPanel::SourcesPanel(QWidget *parent)
    : QWidget(parent)
{
    qDebug() << "SourcesPanel ctor: start";
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    qDebug() << "SourcesPanel ctor: created QListWidget";
    m_edit = new QLineEdit(this);
    qDebug() << "SourcesPanel ctor: created QLineEdit";
    m_edit->setPlaceholderText("subreddit (no r/) e.g. WidescreenWallpaper");
    m_btnAdd = new QPushButton("Add", this);
    qDebug() << "SourcesPanel ctor: created Add button";

    auto *h = new QHBoxLayout;
    h->addWidget(m_edit);
    h->addWidget(m_btnAdd);

    auto *v = new QVBoxLayout(this);
    v->addWidget(m_list);
    v->addLayout(h);
    setLayout(v);

    qDebug() << "SourcesPanel ctor: layout set";

    connect(m_btnAdd, &QPushButton::clicked, this, [this]() {
        QString txt = m_edit->text().trimmed();
        if (txt.isEmpty()) return;
        // normalize: remove leading r/ if present
        if (txt.startsWith("r/")) txt = txt.mid(2);
        // avoid duplicates (compare against stored raw names)
        for (int i=0;i<m_list->count();++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString().compare(txt, Qt::CaseInsensitive) == 0) return;
        }
        createListItem(txt, true);
        m_edit->clear();
        QStringList s = sources();
        emit sourcesChanged(s);
        emit enabledSourcesChanged(enabledSources());
    });

    // Note: the Remove action is available from the item's context menu. The explicit Remove button was removed.

    // react to manual check/uncheck changes
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it){
        Q_UNUSED(it);
        emit enabledSourcesChanged(enabledSources());
    });

    // enable right-click context menu for quick removal
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pt){
        QListWidgetItem *it = m_list->itemAt(pt);
        if (!it) return;
        QString raw = it->data(Qt::UserRole).toString();
        if (raw.isEmpty()) raw = it->text();
    QMenu menu(m_list);
    QAction *actUpdate10 = menu.addAction("Scan last 10 posts");
    QAction *actUpdate50 = menu.addAction("Scan last 50 posts");
    QAction *actUpdate100 = menu.addAction("Scan last 100 posts");
        menu.addSeparator();
        QAction *actRemove = menu.addAction("Remove");
        QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pt));
        if (chosen == actRemove) {
            // remove associated widgets
            if (m_itemWidgets.contains(raw)) {
                delete m_itemWidgets.value(raw);
                m_itemWidgets.remove(raw);
            }
            m_itemLabels.remove(raw);
            m_itemProgress.remove(raw);
            delete it;
            QStringList s = sources();
            emit sourcesChanged(s);
            emit enabledSourcesChanged(enabledSources());
        } else if (chosen == actUpdate10) {
            emit updateRequested(raw, 10);
        } else if (chosen == actUpdate50) {
            emit updateRequested(raw, 50);
        } else if (chosen == actUpdate100) {
            emit updateRequested(raw, 100);
        }
    });
}

// Helper to create a list item and its custom widget (label + progress bar)
void SourcesPanel::createListItem(const QString &raw, bool enabled)
{
    auto *it = new QListWidgetItem(raw, m_list);
    it->setData(Qt::UserRole, raw);
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);

    QWidget *w = new QWidget(m_list);
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(4,2,4,2);
    QLabel *lbl = new QLabel(raw, w);
    QProgressBar *p = new QProgressBar(w);
    p->setRange(0, 100);
    p->setValue(0);
    p->setVisible(false);
    p->setTextVisible(false);
    h->addWidget(lbl);
    h->addStretch();
    h->addWidget(p);
    w->setLayout(h);

    m_itemWidgets.insert(raw, w);
    m_itemLabels.insert(raw, lbl);
    m_itemProgress.insert(raw, p);

    m_list->addItem(it);
    m_list->setItemWidget(it, w);
}

void SourcesPanel::setSources(const QStringList &sources)
{
    // remove existing widgets
    for (auto w : m_itemWidgets) delete w;
    m_itemWidgets.clear();
    m_itemLabels.clear();
    m_itemProgress.clear();
    m_list->clear();
    for (const QString &s : sources) {
        createListItem(s, true);
    }
}

QStringList SourcesPanel::sources() const
{
    QStringList out;
    for (int i=0;i<m_list->count();++i) {
        QListWidgetItem *it = m_list->item(i);
        QString raw = it->data(Qt::UserRole).toString();
        if (raw.isEmpty()) raw = it->text();
        out << raw;
    }
    return out;
}

QStringList SourcesPanel::enabledSources() const
{
    QStringList out;
    for (int i=0;i<m_list->count();++i) {
        QListWidgetItem *it = m_list->item(i);
        if (it->checkState() == Qt::Checked) {
            QString raw = it->data(Qt::UserRole).toString();
            if (raw.isEmpty()) raw = it->text();
            out << raw;
        }
    }
    return out;
}

bool SourcesPanel::loadFromFile(const QString &path)
{
    QFile f(path);
    if (!f.exists()) return false;
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    QStringList s;
    m_lastUpdated.clear();
    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        for (const auto &v : arr) if (v.isString()) s << v.toString();
        // array -> default all enabled
        setSources(s);
    } else if (doc.isObject()) {
        QJsonObject obj = doc.object();
        // expected format: { "subreddit": { "enabled": true, "last_updated": "..." }, ... }
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QString key = it.key();
            const QJsonValue v = it.value();
            s << key;
            if (v.isObject()) {
                QJsonObject entry = v.toObject();
                if (entry.contains("last_updated") && entry.value("last_updated").isString()) {
                    QDateTime dt = QDateTime::fromString(entry.value("last_updated").toString(), Qt::ISODate);
                    if (dt.isValid()) m_lastUpdated.insert(key, dt);
                }
            }
        }
        // now populate list with proper checked state
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            QString key = it.key();
            QJsonValue v = it.value();
            bool enabled = true;
            if (v.isObject()) {
                QJsonObject entry = v.toObject();
                if (entry.contains("enabled")) enabled = entry.value("enabled").toBool(true);
            }
            createListItem(key, enabled);
        }
    } else {
        return false;
    }
    QStringList all = sources();
    emit sourcesChanged(all);
    emit enabledSourcesChanged(enabledSources());
    return true;
}

bool SourcesPanel::saveToFile(const QString &path) const
{
    QJsonObject obj;
    // Save as object mapping subreddit-> { enabled: bool, last_updated: string }
    for (int i=0;i<m_list->count();++i) {
        QListWidgetItem *it = m_list->item(i);
        QString s = it->data(Qt::UserRole).toString();
        if (s.isEmpty()) s = it->text();
        QJsonObject entry;
        entry.insert("enabled", it->checkState() == Qt::Checked);
        if (m_lastUpdated.contains(s)) entry.insert("last_updated", m_lastUpdated.value(s).toString(Qt::ISODate));
        else entry.insert("last_updated", "");
        obj.insert(s, entry);
    }
    QJsonDocument doc(obj);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

QMap<QString, QDateTime> SourcesPanel::lastUpdatedMap() const
{
    return m_lastUpdated;
}

void SourcesPanel::setLastUpdated(const QString &subreddit, const QDateTime &when)
{
    if (subreddit.isEmpty()) return;
    m_lastUpdated.insert(subreddit, when);
}

void SourcesPanel::updateCounts(const QString &cacheDir)
{
    if (cacheDir.isEmpty()) return;
    QString indexPath = cacheDir + "/index.json";
    QFile f(indexPath);
    QMap<QString,int> counts;
    if (f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                QJsonObject entry = it.value().toObject();
                QString sub = entry.value("subreddit").toString();
                if (!sub.isEmpty()) counts[sub] += 1;
            }
        }
    }

    // Update displayed text for each list item to include count
    for (int i=0;i<m_list->count();++i) {
        QListWidgetItem *it = m_list->item(i);
        QString raw = it->data(Qt::UserRole).toString();
        if (raw.isEmpty()) raw = it->text();
        int c = counts.value(raw, 0);
        QString labelText;
        if (c > 0) labelText = QString("%1 (%2)").arg(raw).arg(c);
        else labelText = raw;
        // update label if we created a custom widget for this raw name
        if (m_itemLabels.contains(raw)) {
            m_itemLabels.value(raw)->setText(labelText);
            // Ensure the underlying QListWidgetItem has a sensible tooltip
            it->setToolTip(raw);
        } else {
            // fallback to modifying the item text
            if (c > 0) it->setText(QString("%1 (%2)").arg(raw).arg(c));
            else it->setText(raw);
            it->setToolTip(raw);
        }
    }
}

void SourcesPanel::startUpdateProgress(const QString &subreddit)
{
    if (subreddit.isEmpty()) return;
    if (!m_itemProgress.contains(subreddit)) return;
    QProgressBar *p = m_itemProgress.value(subreddit);
    if (!p) return;
    // indeterminate/progressing
    p->setRange(0, 0);
    p->setVisible(true);
}

void SourcesPanel::setUpdateProgress(const QString &subreddit, int percent)
{
    if (subreddit.isEmpty()) return;
    if (!m_itemProgress.contains(subreddit)) return;
    QProgressBar *p = m_itemProgress.value(subreddit);
    if (!p) return;
    if (percent < 0) {
        p->setRange(0, 0);
    } else {
        if (p->minimum() == 0 && p->maximum() == 0) p->setRange(0, 100);
        p->setValue(qBound(0, percent, 100));
    }
    p->setVisible(true);
}

void SourcesPanel::finishUpdateProgress(const QString &subreddit)
{
    if (subreddit.isEmpty()) return;
    if (!m_itemProgress.contains(subreddit)) return;
    QProgressBar *p = m_itemProgress.value(subreddit);
    if (!p) return;
    p->setVisible(false);
    p->setRange(0, 100);
    p->setValue(0);
}

// No manual moc include; AUTOMOC will generate the necessary meta-object code.
