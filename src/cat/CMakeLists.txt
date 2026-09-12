add_library(cat STATIC cat.cpp civ_protocol.cpp civ_processor.cpp)

# PUBLIC: cat.h is the only header consumers need (cat/cat.h).
target_include_directories(cat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# PRIVATE: meter.h / radio.h / tx_info.h are implementation deps. They must
# not be leaked as -I <project>/src to consumers, which would shadow the
# sysroot's <gps.h> etc. for src/gps.c.
target_include_directories(cat PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cat PUBLIC cfg lvgl aether_x6100_control)
