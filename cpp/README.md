C++ Qt scaffold for Wallpaper

This folder contains a minimal Qt6-based scaffold for a wallpaper application.

Build (Linux example):

  mkdir build && cd build
  cmake ..
  cmake --build .

Run:

  ./wallpaper-qt

Notes:
- CMake fetches the `parsec` JSON library (from https://github.com/matthew-oconnell/parsec) but the current code uses Qt's QJsonDocument for parsing. I'll switch parsing to parsec once you confirm the parsec include and API.
- The app currently only fetches the subreddit JSON and shows a tray notification with a candidate image URL. Download and wallpaper-setting are TODO and can be implemented next.
