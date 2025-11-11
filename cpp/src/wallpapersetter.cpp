#include "wallpapersetter.h"
#include <QProcess>
#include <QDebug>
#include <QFileInfo>
#include <QProcessEnvironment>

WallpaperSetter::WallpaperSetter() {
}

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
    bool result1 = runCommand("gsettings", QStringList() 
        << "set" << "org.gnome.desktop.background" << "picture-uri" 
        << QString("file://%1").arg(imagePath));
    
    bool result2 = runCommand("gsettings", QStringList() 
        << "set" << "org.gnome.desktop.background" << "picture-uri-dark" 
        << QString("file://%1").arg(imagePath));
    
    return result1 || result2;
}

bool WallpaperSetter::setWallpaperKDE(const QString& imagePath) {
    qDebug() << "Using KDE Plasma method";
    
    // KDE Plasma uses D-Bus to set wallpaper
    // This JavaScript command sets wallpaper on all desktops
    QString script = QString(
        "var allDesktops = desktops();"
        "for (i=0; i<allDesktops.length; i++) {"
        "    d = allDesktops[i];"
        "    d.wallpaperPlugin = 'org.kde.image';"
        "    d.currentConfigGroup = Array('Wallpaper', 'org.kde.image', 'General');"
        "    d.writeConfig('Image', 'file://%1');"
        "}"
    ).arg(imagePath);
    
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
    
    if (!process.waitForStarted()) {
        qWarning() << "Failed to start command:" << command;
        return false;
    }
    
    if (!process.waitForFinished(5000)) { // 5 second timeout
        qWarning() << "Command timed out:" << command;
        process.kill();
        return false;
    }
    
    if (process.exitCode() != 0) {
        qWarning() << "Command failed with exit code" << process.exitCode() << ":" << command;
        qWarning() << "stderr:" << process.readAllStandardError();
        return false;
    }
    
    return true;
}
