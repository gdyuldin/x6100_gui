file(GLOB FONTS_SOURCES
    "*.c"
)

target_sources(${PROJECT_NAME} PUBLIC ${FONTS_SOURCES})
