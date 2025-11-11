#ifndef WALLPAPERSETTER_H
#define WALLPAPERSETTER_H

#include <QString>
#include <QStringList>

class WallpaperSetter {
public:
    WallpaperSetter();
    
    // Main function to set wallpaper, auto-detects desktop environment
    bool setWallpaper(const QString& imagePath);
    
    // Desktop environment detection
    QString detectDesktopEnvironment() const;
    
private:
    // Backend implementations
    bool setWallpaperGnome(const QString& imagePath);
    bool setWallpaperKDE(const QString& imagePath);
    bool setWallpaperWithFeh(const QString& imagePath);
    bool setWallpaperXwallpaper(const QString& imagePath);
    
    // Helper to run shell commands
    bool runCommand(const QString& command, const QStringList& args = QStringList());
};

#endif // WALLPAPERSETTER_H
