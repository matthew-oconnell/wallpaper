#include "sourcespanel.h"

#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

SourcesPanel::SourcesPanel(QWidget *parent)
    : QWidget(parent)
{
    qDebug() << "SourcesPanel ctor: start";
    m_list = new QListWidget(this);
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
        // avoid duplicates
        for (int i=0;i<m_list->count();++i) {
            if (m_list->item(i)->text().compare(txt, Qt::CaseInsensitive) == 0) return;
        }
        m_list->addItem(txt);
        m_edit->clear();
        QStringList s = sources();
        emit sourcesChanged(s);
    });

    connect(m_btnRemove, &QPushButton::clicked, this, [this]() {
        auto items = m_list->selectedItems();
        for (QListWidgetItem *it : items) {
            delete it;
        }
        QStringList s = sources();
        emit sourcesChanged(s);
    });
}

void SourcesPanel::setSources(const QStringList &sources)
{
    m_list->clear();
    for (const QString &s : sources) m_list->addItem(s);
}

QStringList SourcesPanel::sources() const
{
    QStringList out;
    for (int i=0;i<m_list->count();++i) out << m_list->item(i)->text();
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
    } else if (doc.isObject()) {
        QJsonObject obj = doc.object();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QString key = it.key();
            const QJsonValue v = it.value();
            s << key;
            if (v.isString()) {
                QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODate);
                if (dt.isValid()) m_lastUpdated.insert(key, dt);
            }
        }
    } else {
        return false;
    }
    setSources(s);
    emit sourcesChanged(s);
    return true;
}

bool SourcesPanel::saveToFile(const QString &path) const
{
    QJsonObject obj;
    // prefer saving as object mapping subreddit->ISO timestamp (or empty string)
    for (const QString &s : sources()) {
        if (m_lastUpdated.contains(s)) {
            obj.insert(s, m_lastUpdated.value(s).toString(Qt::ISODate));
        } else {
            obj.insert(s, "");
        }
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

// No manual moc include; AUTOMOC will generate the necessary meta-object code.
