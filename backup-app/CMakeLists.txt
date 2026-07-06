cmake_minimum_required(VERSION 3.16)
project(BackupApp VERSION 1.0.0 LANGUAGES CXX)

# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt auto-processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt5 (fallback to Qt6 if Qt5 not found)
find_package(Qt5 COMPONENTS Widgets QUIET)
if(NOT Qt5_FOUND)
    find_package(Qt6 COMPONENTS Widgets REQUIRED)
    set(QT_LIBS Qt6::Widgets)
    message(STATUS "Using Qt6")
else()
    set(QT_LIBS Qt5::Widgets)
    message(STATUS "Using Qt5")
endif()

# Find required libraries
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)

# nlohmann/json - try find_package first, then fall back to bundled
find_package(nlohmann_json 3.2.0 QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "Found system nlohmann_json")
    set(JSON_INCLUDE_DIR "")
    set(JSON_LIB nlohmann_json::nlohmann_json)
else()
    message(STATUS "Using bundled nlohmann/json (header-only)")
    set(JSON_LIB "")
endif()

# spdlog - try find_package first, then fall back to bundled
find_package(spdlog QUIET)
if(spdlog_FOUND)
    message(STATUS "Found system spdlog")
    set(SPDLOG_LIB spdlog::spdlog)
else()
    message(STATUS "Using bundled spdlog (header-only)")
    set(SPDLOG_LIB "")
endif()

# Options
option(BUILD_TESTS "Build unit tests" OFF)

# Source subdirectory
add_subdirectory(src)

# Tests (optional)
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()
