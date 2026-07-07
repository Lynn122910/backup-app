# Common include directories
set(COMMON_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR})

# --- Common Library (header-only types + logger + file_utils) ---
add_library(backup_common STATIC
    common/logger.cpp
    common/file_utils.cpp
)
target_include_directories(backup_common PUBLIC ${COMMON_INCLUDE})
target_link_libraries(backup_common PUBLIC ${SPDLOG_LIB})

# --- Services Library ---
add_library(backup_services STATIC
    services/file_scanner.cpp
    services/metadata_handler.cpp
)
target_include_directories(backup_services PUBLIC ${COMMON_INCLUDE})
target_link_libraries(backup_services PUBLIC backup_common)

# --- Core Library ---
add_library(backup_core STATIC
    core/backup_manifest.cpp
    core/backup_archive.cpp
    core/backup_engine.cpp
    core/restore_engine.cpp
    core/config_manager.cpp
)
target_include_directories(backup_core PUBLIC ${COMMON_INCLUDE})
target_link_libraries(backup_core PUBLIC
    backup_services
    ${JSON_LIB}
    ${QT_LIBS}
    ZLIB::ZLIB
    OpenSSL::Crypto
    OpenSSL::SSL
)

# --- GUI Executable ---
add_executable(backup-app
    main.cpp
    ui/main_window.cpp
    ui/backup/backup_panel.cpp
    ui/restore/restore_panel.cpp
)
target_link_libraries(backup-app PRIVATE
    backup_core
    ${QT_LIBS}
)
