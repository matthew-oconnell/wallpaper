#include "redditfetcher.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

std::vector<std::string> RedditFetcher::fetchRecentImageUrls(const QString &subreddit, int limit) {
    QNetworkAccessManager mgr;
    QString url = QString("https://www.reddit.com/r/%1/new.json?limit=%2").arg(subreddit).arg(limit);
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "wallaroo/0.1");
    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    std::vector<std::string> out;
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return out;
    }
    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return out;
    QJsonObject root = doc.object();
    QJsonArray children = root.value("data").toObject().value("children").toArray();
    for (auto v : children) {
        QJsonObject d = v.toObject().value("data").toObject();
        QString url = d.value("url_overridden_by_dest").toString();
        if (url.isEmpty()) url = d.value("url").toString();
        if (url.isEmpty()) continue;
        if (url.endsWith(".jpg") || url.endsWith(".jpeg") || url.endsWith(".png") || url.endsWith(".webp") || url.contains("imgur.com")) {
            out.push_back(url.toStdString());
        }
        // galleries and i.redd.it handled later
    }
    return out;
}
