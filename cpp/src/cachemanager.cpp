#include "cachemanager.h"

#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QCryptographicHash>

QString CacheManager::downloadAndCache(const QString &url) {
    // cache dir under XDG cache
    QString cacheBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheBase.isEmpty()) cacheBase = QDir::homePath() + "/.cache";
    QDir dir(cacheBase);
    if (!dir.exists("wallpaper")) dir.mkdir("wallpaper");
    dir.cd("wallpaper");

    // base filename from URL
    QString name = url.section('/', -1);
    if (name.isEmpty()) name = "wallpaper.jpg";
    QString finalPath = dir.filePath(name);
    // if exists, return
    if (QFile::exists(finalPath)) return finalPath;

    // download
    QNetworkAccessManager mgr;
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "wallpaper-cpp/0.1");
    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return QString();
    }
    QByteArray data = reply->readAll();
    reply->deleteLater();

    // compute sha256 and avoid duplicates
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    // simplistic: filename as hash + ext
    QString ext = name.section('.', -1);
    QString outName = QString::fromUtf8(hash) + "." + ext;
    QString outPath = dir.filePath(outName);
    if (QFile::exists(outPath)) return outPath;
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(data);
    f.close();
    return outPath;
}
