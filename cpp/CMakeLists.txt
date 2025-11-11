cmake_minimum_required(VERSION 3.16)
project(wallpaper-cpp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Widgets Network Core Gui REQUIRED)

include(FetchContent)
# Optional: fetch parsec JSON library. It should be available at the provided repo.
FetchContent_Declare(
  parsec
  GIT_REPOSITORY https://github.com/matthew-oconnell/parsec.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(parsec)

add_executable(wallpaper-cpp
  src/main.cpp
  src/appwindow.h
  src/appwindow.cpp
  src/cachemanager.h
  src/cachemanager.cpp
  src/redditfetcher.h
  src/redditfetcher.cpp
)

target_include_directories(wallpaper-cpp PRIVATE ${parsec_SOURCE_DIR}/include)

target_link_libraries(wallpaper-cpp PRIVATE Qt6::Widgets Qt6::Network Qt6::Core Qt6::Gui)

install(TARGETS wallpaper-cpp RUNTIME DESTINATION bin)
cmake_minimum_required(VERSION 3.16)
project(wallpaper-qt LANGUAGES CXX)

include(FetchContent)

# Fetch parsec (user-specified JSON library). Repository: https://github.com/matthew-oconnell/parsec
FetchContent_Declare(
  parsec
  GIT_REPOSITORY https://github.com/matthew-oconnell/parsec.git
  GIT_TAG        HEAD
)
FetchContent_MakeAvailable(parsec)

find_package(Qt6 COMPONENTS Widgets Network Core REQUIRED)

add_executable(wallpaper-qt
  src/main.cpp
)

target_include_directories(wallpaper-qt PRIVATE ${parsec_SOURCE_DIR}/include)
# If parsec exports a CMake target, the following can be uncommented:
# target_link_libraries(wallpaper-qt PRIVATE parsec)

target_link_libraries(wallpaper-qt PRIVATE Qt6::Widgets Qt6::Network Qt6::Core)

install(TARGETS wallpaper-qt RUNTIME DESTINATION bin)
