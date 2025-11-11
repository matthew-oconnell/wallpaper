#pragma once

#include <QString>
#include <vector>

class RedditFetcher {
public:
    // Return candidate image URLs from the most recent `limit` posts
    std::vector<std::string> fetchRecentImageUrls(const QString &subreddit, int limit = 10);
};
