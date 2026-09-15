add_library(cat_lan STATIC cat_lan.cpp)

target_include_directories(cat_lan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(cat_lan PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cat_lan PUBLIC cat_transport liquid)
