# CAT protocol core: CI-V framing/processing and the scope streamer. No
# transport and no UI; depends on cfg, aether control and LVGL (logging only).
add_library(cat_core STATIC
    civ_protocol.cpp civ_processor.cpp scope_streamer.cpp
)

# PUBLIC: core headers (civ_protocol.h, civ_processor.h, scope_streamer.h).
target_include_directories(cat_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# PRIVATE: ports/cfg headers are reached by relative includes. Must not leak
# -I <project>/src to consumers, which would shadow the sysroot's <gps.h>.
target_include_directories(cat_core PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cat_core PUBLIC cfg lvgl aether_x6100_control)

# CAT transport: UART/Bluetooth poll loop and dispatcher. Application-level
# glue; cat.h is its public header.
add_library(cat_transport STATIC cat.cpp)

target_include_directories(cat_transport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(cat_transport PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cat_transport PUBLIC cat_core cfg)

add_subdirectory(lan)
