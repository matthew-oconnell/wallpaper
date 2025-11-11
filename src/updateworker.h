#pragma once

#include <QObject>
#include <QStringList>

class RedditFetcher;
class CacheManager;

class UpdateWorker : public QObject {
    Q_OBJECT
public:
    UpdateWorker(RedditFetcher *fetcher, CacheManager *cache, const QStringList &subreddits, QObject *parent = nullptr);

public slots:
    void start();

signals:
    void imageCached(const QString &localPath, const QString &subreddit, const QString &sourceUrl);
    void finished();
    void error(const QString &msg);

private:
    RedditFetcher *m_fetcher;
    CacheManager *m_cache;
    QStringList m_subreddits;
};
