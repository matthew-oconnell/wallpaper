#include "cachemanager.h"

#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QCryptographicHash>
#include <QDebug>
#include <QRandomGenerator>

QString CacheManager::downloadAndCache(const QString &url) {
    // Use the same cache directory as the Python app (~/.cache/wallpaper)
    QString cacheBase = QDir::homePath() + "/.cache/wallpaper";
    // ensure the directory exists (mkpath creates parent dirs as needed)
    if (!QDir().mkpath(cacheBase)) {
        qWarning() << "Failed to create cache directory:" << cacheBase;
        // fall back to home cache directory
        cacheBase = QDir::homePath() + "/.cache";
        QDir().mkpath(cacheBase);
    }
    QDir dir(cacheBase);

    // store cacheBase for callers
    // Note: we don't keep state in this simple manager, so cacheDirPath() will compute the same value

    // base filename from URL
    QString name = url.section('/', -1);
    if (name.isEmpty()) name = "wallpaper.jpg";
    QString finalPath = dir.filePath(name);
    // if exists, return
    if (QFile::exists(finalPath)) return finalPath;

    // download
    qDebug() << "Downloading:" << url;
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "wallpaper-cpp/0.1");
    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Network error:" << reply->error() << reply->errorString();
        qWarning() << "URL was:" << url;
        reply->deleteLater();
        return QString();
    }
    QByteArray data = reply->readAll();
    qDebug() << "Downloaded" << data.size() << "bytes";
    reply->deleteLater();

    // compute sha256 and avoid duplicates
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    // simplistic: filename as hash + ext
    QString ext = name.section('.', -1);
    QString outName = QString::fromUtf8(hash) + "." + ext;
    QString outPath = dir.filePath(outName);
    if (QFile::exists(outPath)) {
        qDebug() << "File already exists (by hash):" << outPath;
        return outPath;
    }
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << outPath;
        return QString();
    }
    f.write(data);
    f.close();
    qDebug() << "Saved to:" << outPath;
    return outPath;
}

QString CacheManager::cacheDirPath() const {
    QString cacheBase = QDir::homePath() + "/.cache/wallpaper";
    if (!QDir().exists(cacheBase)) {
        QDir().mkpath(cacheBase);
    }
    return cacheBase;
}

QString CacheManager::randomImagePath() const {
    QString cacheBase = cacheDirPath();
    QDir dir(cacheBase);
    if (!dir.exists()) return QString();
    QStringList nameFilters;
    nameFilters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.webp" << "*.gif";
    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files);
    if (files.isEmpty()) return QString();
    int idx = QRandomGenerator::global()->bounded(files.size());
    return files.at(idx).absoluteFilePath();
}
