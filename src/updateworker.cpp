#include "updateworker.h"
#include "redditfetcher.h"
#include "cachemanager.h"

#include <QThread>
#include <QDebug>

UpdateWorker::UpdateWorker(RedditFetcher *fetcher, CacheManager *cache, const QStringList &subreddits, int perSubLimit, QObject *parent)
    : QObject(parent), m_fetcher(fetcher), m_cache(cache), m_subreddits(subreddits), m_perSubLimit(perSubLimit)
{
}

void UpdateWorker::start()
{
    if (!m_fetcher || !m_cache) {
        emit error("Missing fetcher or cache manager");
        emit finished();
        return;
    }

    qDebug() << "UpdateWorker: starting update for" << m_subreddits.size() << "subreddits";
    for (const QString &sub : m_subreddits) {
        // Fetch up to m_perSubLimit most recent posts for this subreddit
        std::vector<std::string> urls = m_fetcher->fetchRecentImageUrls(sub, m_perSubLimit);
        qDebug() << "UpdateWorker: subreddit" << sub << "returned" << (int)urls.size() << "urls";
            // emit started with total count
            emit started(sub, (int)urls.size());
        for (const std::string &surl : urls) {
            QString url = QString::fromStdString(surl);
            QString local = m_cache->downloadAndCache(url);
            if (!local.isEmpty()) {
                emit imageCached(local, sub, url);
            }
            // gentle throttle to avoid hammering services
            QThread::msleep(150);
        }
            // emit finished for this subreddit
            emit finishedSubreddit(sub);
    }

    emit finished();
}
