#pragma once

#include <QWidget>
#include <QStringList>
#include <QDateTime>
#include <QMap>

class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;

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

    // Progress UI helpers: show/advance/finish a per-subreddit progress bar
    void startUpdateProgress(const QString &subreddit);
    void setUpdateProgress(const QString &subreddit, int percent);
    void finishUpdateProgress(const QString &subreddit);

signals:
    void sourcesChanged(const QStringList &sources);
    // Emitted when the set of enabled (checked) subreddits changes
    void enabledSourcesChanged(const QStringList &enabled);
    // Request an update for a single subreddit with the given per-subreddit limit
    void updateRequested(const QString &subreddit, int perSubLimit);


private:
    // Create a QListWidgetItem and its corresponding custom widget (label + progress bar)
    void createListItem(const QString &raw, bool enabled = true);
    QListWidget *m_list = nullptr;
    QLineEdit *m_edit = nullptr;
    QPushButton *m_btnAdd = nullptr;
    QPushButton *m_btnRemove = nullptr;
    QMap<QString, QDateTime> m_lastUpdated;
    // Per-item widgets
    QMap<QString, QWidget*> m_itemWidgets;
    QMap<QString, QLabel*> m_itemLabels;
    QMap<QString, QProgressBar*> m_itemProgress;
};
