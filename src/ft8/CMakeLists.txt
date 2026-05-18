add_library(FT8 STATIC
    qso.cpp worker.c utils.c gfsk.c
    ft8_log.c params.c
    table_view.c audio_worker.c auto_dnf.c
    auto_sel.cpp qso_state.c tx_worker.c cq_scheduler.c
)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../qth")
