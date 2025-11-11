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
#include <QSaveFile>
#include <QRunnable>
#include <QThreadPool>
#include <QMutex>
#include <QFileInfo>

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
        // schedule an async task to ensure index.json contains size/thumbnail for this file
        class EnsureIndexTask : public QRunnable {
        public:
            EnsureIndexTask(const QString &outPath_, const QString &outName_, const QByteArray &hash_, const QString &dirPath_)
                : outPath(outPath_), outName(outName_), hash(hash_), dirPath(dirPath_) {}
            void run() override {
                QImageReader rExist(outPath);
                QSize sz = rExist.size();
                if (sz.isEmpty()) {
                    QImage imgExist(outPath);
                    if (!imgExist.isNull()) sz = imgExist.size();
                }
                QString indexPath = QDir(dirPath).filePath("index.json");
                QJsonObject rootObj;
                QFile idxf(indexPath);
                if (idxf.open(QIODevice::ReadOnly)) {
                    QJsonDocument doc = QJsonDocument::fromJson(idxf.readAll());
                    if (doc.isObject()) rootObj = doc.object();
                    idxf.close();
                }
                QJsonObject entry = rootObj.value(outName).toObject();
                bool changed = false;
                if (!sz.isEmpty() && (!entry.contains("width") || !entry.contains("height"))) {
                    entry["width"] = sz.width();
                    entry["height"] = sz.height();
                    changed = true;
                }
                QString thumbName = QString::fromUtf8(hash) + "-thumb.jpg";
                QString thumbPath = QDir(dirPath).filePath(thumbName);
                if (!entry.contains("thumbnail") || !QFile::exists(thumbPath)) {
                    QImage img2(outPath);
                    if (!img2.isNull()) {
                        QImage thumb = img2.scaled(300,300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        thumb.save(thumbPath, "JPEG", 85);
                    }
                    entry["thumbnail"] = thumbName;
                    changed = true;
                }
                if (changed) {
                    rootObj[outName] = entry;
                    QSaveFile sf(indexPath);
                    if (sf.open(QIODevice::WriteOnly)) {
                        sf.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
                        sf.commit();
                    }
                }
            }
        private:
            QString outPath;
            QString outName;
            QByteArray hash;
            QString dirPath;
        };
    QThreadPool::globalInstance()->start(new EnsureIndexTask(outPath, outName, hash, dir.absolutePath()));
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

    // After saving, schedule thumbnail generation and index.json updates on a background thread
    QString outNameCopy2 = outName;
    QByteArray hashCopy2 = hash;
    QString outPathCopy = outPath;
    QString dirPath2 = dir.absolutePath();
    class GenerateThumbTask : public QRunnable {
    public:
        GenerateThumbTask(const QString &outPath_, const QString &outName_, const QByteArray &hash_, const QString &dirPath_)
            : outPath(outPath_), outName(outName_), hash(hash_), dirPath(dirPath_) {}
        void run() override {
            QSize sz;
            QImageReader r(outPath);
            sz = r.size();
            QImage img;
            if (sz.isEmpty()) {
                img = QImage(outPath);
                if (!img.isNull()) sz = img.size();
            }
            QString thumbName;
            if (!img.isNull()) {
                thumbName = QString::fromUtf8(hash) + "-thumb.jpg";
                QString thumbPath = QDir(dirPath).filePath(thumbName);
                QImage thumb = img.scaled(300, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                thumb.save(thumbPath, "JPEG", 85);
            }
            QString indexPath = QDir(dirPath).filePath("index.json");
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
            entry["downloaded_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            if (!entry.contains("favorite")) entry["favorite"] = false;
            if (!entry.contains("banned")) entry["banned"] = false;
            rootObj[outName] = entry;
            QSaveFile sf(indexPath);
            if (sf.open(QIODevice::WriteOnly)) {
                sf.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
                sf.commit();
            } else {
                qWarning() << "Failed to write index.json" << indexPath;
            }
        }
    private:
        QString outPath;
        QString outName;
        QByteArray hash;
        QString dirPath;
    };
    QThreadPool::globalInstance()->start(new GenerateThumbTask(outPathCopy, outNameCopy2, hashCopy2, dirPath2));
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
