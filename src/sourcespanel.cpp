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

SourcesPanel::SourcesPanel(QWidget *parent)
    : QWidget(parent)
{
    qDebug() << "SourcesPanel ctor: start";
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    qDebug() << "SourcesPanel ctor: created QListWidget";
    m_edit = new QLineEdit(this);
    qDebug() << "SourcesPanel ctor: created QLineEdit";
    m_edit->setPlaceholderText("subreddit (without r/) e.g. WidescreenWallpaper");
    m_btnAdd = new QPushButton("Add", this);
    qDebug() << "SourcesPanel ctor: created Add button";
    m_btnRemove = new QPushButton("Remove", this);
    qDebug() << "SourcesPanel ctor: created Remove button";

    auto *h = new QHBoxLayout;
    h->addWidget(m_edit);
    h->addWidget(m_btnAdd);
    h->addWidget(m_btnRemove);

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
        auto *it = new QListWidgetItem(txt, m_list);
        // store the raw subreddit name in UserRole; displayed text can later include counts
        it->setData(Qt::UserRole, txt);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        m_list->addItem(it);
        m_edit->clear();
        QStringList s = sources();
        emit sourcesChanged(s);
        emit enabledSourcesChanged(enabledSources());
    });

    connect(m_btnRemove, &QPushButton::clicked, this, [this]() {
        auto items = m_list->selectedItems();
        for (QListWidgetItem *it : items) {
            delete it;
        }
        QStringList s = sources();
        emit sourcesChanged(s);
        emit enabledSourcesChanged(enabledSources());
    });

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
        QMenu menu(m_list);
        QAction *actRemove = menu.addAction("Remove");
        QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pt));
        if (chosen == actRemove) {
            delete it;
            QStringList s = sources();
            emit sourcesChanged(s);
            emit enabledSourcesChanged(enabledSources());
        }
    });
}

void SourcesPanel::setSources(const QStringList &sources)
{
    m_list->clear();
    for (const QString &s : sources) {
        auto *it = new QListWidgetItem(s, m_list);
        it->setData(Qt::UserRole, s);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        m_list->addItem(it);
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
        m_list->clear();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            QString key = it.key();
            QJsonValue v = it.value();
            auto *itw = new QListWidgetItem(key, m_list);
            // store raw subreddit name in UserRole
            itw->setData(Qt::UserRole, key);
            itw->setFlags(itw->flags() | Qt::ItemIsUserCheckable);
            bool enabled = true;
            if (v.isObject()) {
                QJsonObject entry = v.toObject();
                if (entry.contains("enabled")) enabled = entry.value("enabled").toBool(true);
            }
            itw->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
            m_list->addItem(itw);
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
        if (c > 0) it->setText(QString("%1 (%2)").arg(raw).arg(c));
        else it->setText(raw);
        it->setToolTip(raw);
    }
}

// No manual moc include; AUTOMOC will generate the necessary meta-object code.
