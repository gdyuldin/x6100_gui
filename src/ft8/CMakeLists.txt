add_library(FT8 STATIC
    qso.cpp worker.c utils.c gfsk.c
    table_view.c
    cq_scheduler.c
    tx_worker.c
    audio_worker.c
    ft8_log.c
)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/..")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../qth")

