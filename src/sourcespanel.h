#pragma once

#include <QWidget>
#include <QStringList>
#include <QDateTime>
#include <QMap>

class QListWidget;
class QLineEdit;
class QPushButton;

class SourcesPanel : public QWidget {
    Q_OBJECT
public:
    explicit SourcesPanel(QWidget *parent = nullptr);

    void setSources(const QStringList &sources);
    QStringList sources() const;
    QStringList enabledSources() const;
    // last-updated timestamps per subreddit (may be null/invalid if never updated)
    QMap<QString, QDateTime> lastUpdatedMap() const;
    void setLastUpdated(const QString &subreddit, const QDateTime &when);
    // Update displayed per-subreddit cached counts by reading index.json in cacheDir
    void updateCounts(const QString &cacheDir);

    // load/save as a JSON array in a file
    bool loadFromFile(const QString &path);
    bool saveToFile(const QString &path) const;

signals:
    void sourcesChanged(const QStringList &sources);
    // Emitted when the set of enabled (checked) subreddits changes
    void enabledSourcesChanged(const QStringList &enabled);

private:
    QListWidget *m_list = nullptr;
    QLineEdit *m_edit = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnRemove = nullptr;
    QMap<QString, QDateTime> m_lastUpdated;
};
