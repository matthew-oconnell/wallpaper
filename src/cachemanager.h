#pragma once

#include <QString>

class CacheManager {
public:
    // Download URL to cache and return local path (QString)
    QString downloadAndCache(const QString &url);
    
    // Return the cache directory path used by the manager
    QString cacheDirPath() const;

    // Return a random image path from the cache, or empty string if none
    QString randomImagePath() const;
};
