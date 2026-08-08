file(GLOB APP_CFG_SOURCES
    "*.cpp"
    "*.c"
)

add_library(app_cfg STATIC ${APP_CFG_SOURCES})

target_include_directories(app_cfg PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/lvgl
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 QUIET IMPORTED_TARGET sqlite3)
else()
    find_library(SQLITE3_LIBRARY sqlite3)
endif()

if(SQLITE3_FOUND)
    target_link_libraries(app_cfg PUBLIC PkgConfig::SQLITE3)
else()
    target_include_directories(app_cfg PUBLIC ${SQLITE3_INCLUDE_DIRS})
    target_link_libraries(app_cfg PUBLIC ${SQLITE3_LIBRARY})
endif()

target_link_libraries(app_cfg PUBLIC Threads::Threads lvgl aether_x6100_control)
