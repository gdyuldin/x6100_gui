add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PUBLIC
    main.c main_screen.c
    styles.c spectrum.c radio.c dsp.cpp util.cpp
    waterfall.c rotary.c keyboard.c encoder.c
    events.c msg.c msg_tiny.c keypad.c
    hkey.c clock.c info.c
    meter.c band_info.c tx_info.c
    audio.c mfk.cpp cw.cpp cw_decoder.c panel.cpp
    rtty.c screenshot.c backlight.c gps.c cat.cpp
    dialog.c dialog_settings.cpp dialog_swrscan.c
    dialog_ft8.c dialog_freq.c dialog_gps.c dialog_msg_cw.c
    dialog_msg_voice.c dialog_recorder.c dialog_qth.c dialog_callsign.c
    textarea_window.c cw_encoder.c buttons.cpp vol.cpp recorder.c
    voice.cpp cw_tune_ui.c adif.c qso_log.c scheduler.cpp
    dialog_wifi.c wifi.cpp controls.cpp usb_devices.cpp
    knobs.cpp
)

add_subdirectory(fonts)
add_subdirectory(ft8)
add_subdirectory(widgets)
add_subdirectory(params)
add_subdirectory(qth)
add_subdirectory(cfg)

include_directories(utf8)
include_directories(${CMAKE_SYSROOT}/usr/include/RHVoice/)
include_directories(${CMAKE_SYSROOT}/usr/include/ft8lib/)

set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    libnm dbus-glib-1 sqlite3 libpng libgps sndfile libudev libpulse libpulse-simple alsa
)


target_link_libraries(${PROJECT_NAME} PRIVATE
PkgConfig::deps
FT8 QTH
Threads::Threads
lvgl lvgl::drivers
aether_x6100_control
liquid
RHVoice RHVoice_core RHVoice_audio
ft8
)

# target_compile_options(${PROJECT_NAME} PRIVATE -g -fno-omit-frame-pointer -fasynchronous-unwind-tables)
# target_link_options(${PROJECT_NAME} PRIVATE -g -rdynamic)

# target_compile_options(${PROJECT_NAME} PRIVATE -g -fsanitize=address -fsanitize=undefined  -fno-omit-frame-pointer -fno-sanitize-recover)
# target_link_options(${PROJECT_NAME} PRIVATE -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -fno-sanitize-recover -static-libasan -static-libubsan)
