#include "updateworker.h"
#include "redditfetcher.h"
#include "cachemanager.h"

#include <QThread>
#include <QDebug>

UpdateWorker::UpdateWorker(RedditFetcher *fetcher, CacheManager *cache, const QStringList &subreddits, QObject *parent)
    : QObject(parent), m_fetcher(fetcher), m_cache(cache), m_subreddits(subreddits)
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
        // Fetch up to 10 most recent posts for this subreddit
        std::vector<std::string> urls = m_fetcher->fetchRecentImageUrls(sub, 10);
        qDebug() << "UpdateWorker: subreddit" << sub << "returned" << (int)urls.size() << "urls";
        for (const std::string &surl : urls) {
            QString url = QString::fromStdString(surl);
            QString local = m_cache->downloadAndCache(url);
            if (!local.isEmpty()) {
                emit imageCached(local, sub, url);
            }
            // gentle throttle to avoid hammering services
            QThread::msleep(150);
        }
    }

    emit finished();
}
