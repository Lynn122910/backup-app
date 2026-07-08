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

# ── Optional libraries for advanced metadata ──────────────────
# libacl  — POSIX ACL support (getfacl/setfacl)
find_library(LIBACL_LIB acl)
find_path(LIBACL_INCLUDE sys/acl.h)
if(LIBACL_LIB AND LIBACL_INCLUDE)
    set(HAVE_ACL TRUE)
    message(STATUS "Found libacl — POSIX ACL support enabled")
else()
    message(STATUS "libacl not found — POSIX ACL support disabled")
endif()

# libcap  — Linux capabilities support (getcap/setcap)
find_library(LIBCAP_LIB cap)
find_path(LIBCAP_INCLUDE sys/capability.h)
if(LIBCAP_LIB AND LIBCAP_INCLUDE)
    set(HAVE_CAP TRUE)
    message(STATUS "Found libcap — Linux capabilities support enabled")
else()
    message(STATUS "libcap not found — capabilities support disabled")
endif()

# libselinux — SELinux context support
find_library(LIBSELINUX_LIB selinux)
find_path(LIBSELINUX_INCLUDE selinux/selinux.h)
if(LIBSELINUX_LIB AND LIBSELINUX_INCLUDE)
    set(HAVE_SELINUX TRUE)
    message(STATUS "Found libselinux — SELinux context support enabled")
else()
    message(STATUS "libselinux not found — SELinux context support disabled")
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
