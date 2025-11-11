#pragma once

#include <QString>

class CacheManager {
public:
    // Download URL to cache and return local path (QString)
    QString downloadAndCache(const QString &url);
};
