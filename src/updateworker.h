#pragma once

#include <QObject>
#include <QStringList>

class RedditFetcher;
class CacheManager;

class UpdateWorker : public QObject {
    Q_OBJECT
public:
    UpdateWorker(RedditFetcher *fetcher, CacheManager *cache, const QStringList &subreddits, int perSubLimit = 10, QObject *parent = nullptr);

public slots:
    void start();

signals:
    void imageCached(const QString &localPath, const QString &subreddit, const QString &sourceUrl);
    // per-subreddit progress notifications
    void started(const QString &subreddit, int total);
    void progress(const QString &subreddit, int completed, int total);
    void finishedSubreddit(const QString &subreddit);
    void finished();
    void error(const QString &msg);

private:
    RedditFetcher *m_fetcher;
    CacheManager *m_cache;
    QStringList m_subreddits;
    int m_perSubLimit = 10;
};
