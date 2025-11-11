#include "wallpapersetter.h"
#include <QProcess>
#include <QDebug>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QString>
#include <QUrl>

WallpaperSetter::WallpaperSetter() {
}

// (m_lastError is an instance member declared in the header)


QString WallpaperSetter::detectDesktopEnvironment() const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    
    // Check common environment variables
    QString xdgCurrentDesktop = env.value("XDG_CURRENT_DESKTOP", "").toLower();
    QString desktopSession = env.value("DESKTOP_SESSION", "").toLower();
    QString gdmSession = env.value("GDMSESSION", "").toLower();
    
    // GNOME detection
    if (xdgCurrentDesktop.contains("gnome") || 
        desktopSession.contains("gnome") || 
        gdmSession.contains("gnome")) {
        return "gnome";
    }
    
    // KDE Plasma detection
    if (xdgCurrentDesktop.contains("kde") || 
        desktopSession.contains("plasma") || 
        desktopSession.contains("kde")) {
        return "kde";
    }
    
    // Check if running under X11 or Wayland
    QString sessionType = env.value("XDG_SESSION_TYPE", "").toLower();
    if (sessionType == "x11") {
        return "x11";
    } else if (sessionType == "wayland") {
        return "wayland";
    }
    
    return "unknown";
}

bool WallpaperSetter::setWallpaper(const QString& imagePath) {
    // Validate image path exists
    QFileInfo fileInfo(imagePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qWarning() << "Image file does not exist:" << imagePath;
        return false;
    }
    
    QString absolutePath = fileInfo.absoluteFilePath();
    qDebug() << "Setting wallpaper to:" << absolutePath;
    
    QString desktop = detectDesktopEnvironment();
    qDebug() << "Detected desktop environment:" << desktop;
    
    bool success = false;
    
    if (desktop == "gnome") {
        success = setWallpaperGnome(absolutePath);
    } else if (desktop == "kde") {
        success = setWallpaperKDE(absolutePath);
    } else if (desktop == "x11" || desktop == "unknown") {
        // Try X11 methods as fallback
        success = setWallpaperWithFeh(absolutePath);
        if (!success) {
            success = setWallpaperXwallpaper(absolutePath);
        }
    }
    
    if (success) {
        qDebug() << "Wallpaper set successfully";
    } else {
        qWarning() << "Failed to set wallpaper";
    }
    
    return success;
}

bool WallpaperSetter::setWallpaperGnome(const QString& imagePath) {
    qDebug() << "Using GNOME gsettings method";
    
    // Try both picture-uri and picture-uri-dark for better compatibility
    QString uri = QUrl::fromLocalFile(imagePath).toString();
    bool result1 = runCommand("gsettings", QStringList()
        << "set" << "org.gnome.desktop.background" << "picture-uri"
        << uri);

    bool result2 = runCommand("gsettings", QStringList()
        << "set" << "org.gnome.desktop.background" << "picture-uri-dark"
        << uri);
    
    return result1 || result2;
}

bool WallpaperSetter::setWallpaperKDE(const QString& imagePath) {
    qDebug() << "Using KDE Plasma method";
    
    // KDE Plasma uses D-Bus to set wallpaper
    // This JavaScript command sets wallpaper on all desktops
    QString uri = QUrl::fromLocalFile(imagePath).toString();
    QString script = QString(
        "var allDesktops = desktops();"
        "for (i=0; i<allDesktops.length; i++) {"
        "    d = allDesktops[i];"
        "    d.wallpaperPlugin = 'org.kde.image';"
        "    d.currentConfigGroup = Array('Wallpaper', 'org.kde.image', 'General');"
        "    d.writeConfig('Image', '%1');"
        "}"
    ).arg(uri);
    
    return runCommand("qdbus", QStringList() 
        << "org.kde.plasmashell" << "/PlasmaShell" 
        << "org.kde.PlasmaShell.evaluateScript" << script);
}

bool WallpaperSetter::setWallpaperWithFeh(const QString& imagePath) {
    qDebug() << "Trying feh method";
    
    // Check if feh is available
    QProcess checkFeh;
    checkFeh.start("which", QStringList() << "feh");
    checkFeh.waitForFinished();
    
    if (checkFeh.exitCode() != 0) {
        qDebug() << "feh not found";
        return false;
    }
    
    return runCommand("feh", QStringList() << "--bg-scale" << imagePath);
}

bool WallpaperSetter::setWallpaperXwallpaper(const QString& imagePath) {
    qDebug() << "Trying xwallpaper method";
    
    // Check if xwallpaper is available
    QProcess checkXwallpaper;
    checkXwallpaper.start("which", QStringList() << "xwallpaper");
    checkXwallpaper.waitForFinished();
    
    if (checkXwallpaper.exitCode() != 0) {
        qDebug() << "xwallpaper not found";
        return false;
    }
    
    return runCommand("xwallpaper", QStringList() << "--zoom" << imagePath);
}

bool WallpaperSetter::runCommand(const QString& command, const QStringList& args) {
    QProcess process;
    process.start(command, args);

    m_lastError.clear();

    if (!process.waitForStarted()) {
        m_lastError = QString("Failed to start command: %1").arg(command);
        qWarning() << m_lastError;
        return false;
    }

    if (!process.waitForFinished(5000)) { // 5 second timeout
        m_lastError = QString("Command timed out: %1").arg(command);
        qWarning() << m_lastError;
        process.kill();
        return false;
    }

    QByteArray stdoutData = process.readAllStandardOutput();
    QByteArray stderrData = process.readAllStandardError();

    if (process.exitCode() != 0) {
        m_lastError = QString("Command failed (exit %1): %2\nstdout:\n%3\nstderr:\n%4")
            .arg(process.exitCode())
            .arg(command)
            .arg(QString::fromUtf8(stdoutData))
            .arg(QString::fromUtf8(stderrData));
        qWarning() << m_lastError;
        return false;
    }

    // store any non-empty stderr/stdout in lastError for diagnostics, but treat as success
    if (!stderrData.isEmpty() || !stdoutData.isEmpty()) {
        m_lastError = QString("Command succeeded: %1\nstdout:\n%2\nstderr:\n%3")
            .arg(command)
            .arg(QString::fromUtf8(stdoutData))
            .arg(QString::fromUtf8(stderrData));
    } else {
        m_lastError.clear();
    }

    return true;
}

QString WallpaperSetter::lastError() const {
    return m_lastError;
}
