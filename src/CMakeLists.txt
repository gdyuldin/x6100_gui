add_executable(${PROJECT_NAME})

# Regression gate: an implicit function declaration is latent UB (the compiler
# assumes int() and default argument promotions), so treat it as an error for
# the application sources only. External libraries/submodules are not affected.
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)

target_sources(${PROJECT_NAME} PUBLIC
    main.c main_screen.c
    styles.c spectrum.c radio.c dsp.cpp util.cpp
    app_ports.cpp
    waterfall.c rotary.cpp keyboard.c encoder.c
    events.c msg.c msg_tiny.c keypad.c
    hkey.c clock.c
    meter.c band_info.cpp tx_info.c
    audio.c mfk.cpp cw.cpp cw_decoder.c panel.cpp
    rtty.c screenshot.c display.cpp gps.c
    dialog.c dialog_settings.cpp dialog_swrscan.c
    dialog_ft8.c dialog_freq.c dialog_gps.c dialog_msg_cw.c
    ft8_ui/table_view.c
    dialog_msg_voice.c dialog_recorder.c dialog_qth.c dialog_callsign.c
    textarea_window.c cw_encoder.c buttons.cpp vol.cpp recorder.c
    voice.cpp cw_tune_ui.c adif.c qso_log.c scheduler.cpp
    format.c msg_cw_store.c
    dialog_wifi.c wifi.cpp controls.cpp usb_devices.cpp
    knobs.cpp lock_manager.c indicators.cpp freq_info.cpp
)

add_subdirectory(fonts)
add_subdirectory(ft8)
add_subdirectory(widgets)
add_subdirectory(qth)
add_subdirectory(cfg)
add_subdirectory(cat)

include_directories(utf8)
include_directories(${CMAKE_SYSROOT}/usr/include/RHVoice/)
include_directories(${CMAKE_SYSROOT}/usr/include/ft8lib/)

set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    libnm dbus-glib-1 sqlite3 libpng libgps sndfile libudev libpulse libpulse-simple alsa bluez
)


target_link_libraries(${PROJECT_NAME} PRIVATE
PkgConfig::deps
ft8_core ft8_app QTH
cfg
cat_core cat_transport
cat_lan
Threads::Threads
lvgl lvgl::drivers
aether_x6100_control
liquid
RHVoice RHVoice_core RHVoice_audio
ft8
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(${PROJECT_NAME} PRIVATE -g -fno-omit-frame-pointer -fasynchronous-unwind-tables)
    target_link_options(${PROJECT_NAME} PRIVATE -g -rdynamic)
endif()

# target_compile_options(${PROJECT_NAME} PRIVATE -g -fsanitize=address -fsanitize=undefined  -fno-omit-frame-pointer -fno-sanitize-recover)
# target_link_options(${PROJECT_NAME} PRIVATE -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -fno-sanitize-recover -static-libasan -static-libubsan)
