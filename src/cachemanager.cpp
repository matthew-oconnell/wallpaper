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
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

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
        // ensure index.json contains size/thumbnail for this file (cheap idempotent update)
        QString indexPath = dir.filePath("index.json");
        QJsonObject rootObj;
        QFile idxf(indexPath);
        if (idxf.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(idxf.readAll());
            if (doc.isObject()) rootObj = doc.object();
            idxf.close();
        }
        QJsonObject entry = rootObj.value(outName).toObject();
        bool needWrite = false;
        QSize sz;
        QImageReader rExist(outPath);
        sz = rExist.size();
        if (sz.isEmpty()) {
            QImage imgExist(outPath);
            if (!imgExist.isNull()) sz = imgExist.size();
        }
        if (!sz.isEmpty() && (!entry.contains("width") || !entry.contains("height"))) {
            entry["width"] = sz.width();
            entry["height"] = sz.height();
            needWrite = true;
        }
        if (!entry.contains("thumbnail")) {
            QString thumbName = QString::fromUtf8(hash) + "-thumb.jpg";
            QString thumbPath = dir.filePath(thumbName);
            if (!QFile::exists(thumbPath)) {
                QImage img2(outPath);
                if (!img2.isNull()) {
                    QImage thumb = img2.scaled(300,300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    thumb.save(thumbPath, "JPEG", 85);
                    qDebug() << "Wrote thumbnail for existing file:" << thumbPath;
                }
            }
            entry["thumbnail"] = thumbName;
            needWrite = true;
        }
        if (needWrite) {
            rootObj[outName] = entry;
            if (idxf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                idxf.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
                idxf.close();
            }
        }
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

    // After saving, compute image dimensions (try header first) and generate a small thumbnail.
    QSize sz;
    QImageReader r(outPath);
    sz = r.size();
    QImage img;
    bool haveImage = false;
    if (sz.isEmpty()) {
        // fall back to full load
        img = QImage(outPath);
        if (!img.isNull()) {
            sz = img.size();
            haveImage = true;
        }
    }
    // if QImageReader provided size, we may still need the full image to make a thumbnail
    if (!haveImage) {
        // attempt to read full image for creating thumbnail
        img = QImage(outPath);
        if (!img.isNull()) haveImage = true;
    }

    // create a thumbnail if we have an image
    QString thumbName;
    if (haveImage) {
        // thumbnail filename based on hash
        thumbName = QString::fromUtf8(hash) + "-thumb.jpg";
        QString thumbPath = dir.filePath(thumbName);
        // scale to a reasonable thumbnail size (max 300 px)
        QImage thumb = img.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        // save as jpeg
        thumb.save(thumbPath, "JPEG", 85);
        qDebug() << "Wrote thumbnail:" << thumbPath;
    }

    // update index.json with width/height and thumbnail path
    QString indexPath = dir.filePath("index.json");
    QJsonObject rootObj;
    QFile idxf(indexPath);
    if (idxf.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(idxf.readAll());
        if (doc.isObject()) rootObj = doc.object();
        idxf.close();
    }
    QJsonObject entry = rootObj.value(outName).toObject();
    if (!sz.isEmpty()) {
        entry["width"] = sz.width();
        entry["height"] = sz.height();
    }
    if (!thumbName.isEmpty()) entry["thumbnail"] = thumbName;
    // record downloaded time
    entry["downloaded_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (!entry.contains("score")) entry["score"] = 0;
    if (!entry.contains("banned")) entry["banned"] = false;
    rootObj[outName] = entry;
    QJsonDocument outDoc(rootObj);
    if (idxf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        idxf.write(outDoc.toJson(QJsonDocument::Indented));
        idxf.close();
    } else {
        qWarning() << "Failed to write index.json" << indexPath;
    }
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
