#include <QApplication>
#include "appwindow.h"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    AppWindow w;
    w.show();
    return app.exec();
}
#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QDebug>

#include <random>

// NOTE: parsec is fetched by CMake (see CMakeLists). Replace QJson-based parsing
// below with parsec usage once you confirm parsec's include path and API.

static const char* USER_AGENT = "wallpaper-cpp/0.1 (by local-script)";

QString pickImageUrlFromListing(const QByteArray &body) {
    // Simple Qt JSON parsing to find first direct image URL candidate
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return {};
    auto data = doc.object().value("data").toObject();
    auto children = data.value("children").toArray();
    QStringList candidates;
    for (const auto &c : children) {
        auto obj = c.toObject().value("data").toObject();
        QString url = obj.value("url_overridden_by_dest").toString();
        if (url.isEmpty()) url = obj.value("url").toString();
        if (url.isEmpty()) continue;
        // simple heuristics: prefer direct images
        if (url.endsWith(".jpg", Qt::CaseInsensitive) || url.endsWith(".jpeg", Qt::CaseInsensitive)
            || url.endsWith(".png", Qt::CaseInsensitive) || url.endsWith(".webp", Qt::CaseInsensitive)) {
            candidates << url;
        } else if (url.contains("imgur.com") && !url.contains("/a/")) {
            candidates << (url + ".jpg");
        }
    }
    if (candidates.isEmpty()) return {};
    // pick random candidate
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, candidates.size() - 1);
    return candidates.at(dist(gen));
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    QSystemTrayIcon tray;
    tray.setIcon(QIcon::fromTheme("image-x-generic"));
    QMenu menu;

    QAction *actionNew = new QAction("New Random Wallpaper", &menu);
    QAction *actionQuit = new QAction("Quit", &menu);
    menu.addAction(actionNew);
    menu.addAction(actionQuit);
    tray.setContextMenu(&menu);
    tray.show();

    QNetworkAccessManager *mgr = new QNetworkAccessManager(&app);

    QObject::connect(actionQuit, &QAction::triggered, &app, &QApplication::quit);

    QObject::connect(actionNew, &QAction::triggered, [&]() {
        // Fetch recent posts from subreddit
        QNetworkRequest req(QUrl("https://www.reddit.com/r/WidescreenWallpaper/new.json?limit=10"));
        req.setRawHeader("User-Agent", USER_AGENT);
        QNetworkReply *reply = mgr->get(req);
        QObject::connect(reply, &QNetworkReply::finished, [reply, &tray]() {
            if (reply->error() != QNetworkReply::NoError) {
                tray.showMessage("Wallpaper", QString("Network error: %1").arg(reply->errorString()));
                reply->deleteLater();
                return;
            }
            QByteArray body = reply->readAll();
            QString url = pickImageUrlFromListing(body);
            if (url.isEmpty()) {
                tray.showMessage("Wallpaper", "No suitable image found.");
                reply->deleteLater();
                return;
            }
            tray.showMessage("Wallpaper", QString("Would set wallpaper from: %1").arg(url));
            // TODO: download the image and set wallpaper (platform-specific)
            reply->deleteLater();
        });
    });

    return app.exec();
}
