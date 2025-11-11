cmake_minimum_required(VERSION 3.16)
project(wallpaper-cpp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets Network Core Gui REQUIRED)

add_executable(wallpaper-cpp
  src/main.cpp
  src/appwindow.h
  src/appwindow.cpp
  src/cachemanager.h
  src/cachemanager.cpp
  src/redditfetcher.h
  src/redditfetcher.cpp
  src/wallpapersetter.h
  src/wallpapersetter.cpp
  src/thumbnailviewer.h
  src/thumbnailviewer.cpp
  src/sourcespanel.h
  src/sourcespanel.cpp
)
target_link_libraries(wallpaper-cpp PRIVATE Qt6::Widgets Qt6::Network Qt6::Core Qt6::Gui)

install(TARGETS wallpaper-cpp RUNTIME DESTINATION bin)
