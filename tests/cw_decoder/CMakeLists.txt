file(GLOB TEST_SOURCES "*.cpp")
add_executable(test_cw_decoder ${TEST_SOURCES})
target_link_libraries(test_cw_decoder PRIVATE cw_decoder Catch2::Catch2WithMain)
add_test(NAME test_cw_decoder COMMAND $<TARGET_FILE:test_cw_decoder> --colour-mode=ansi)

# Measurement tool (not a test): only built, never run by ctest, so `ctest`
# stays fast. The glob above does not descend into tools/, hence its own target.
# Run: cmake --build build_test --target hist_forget_sweep &&
#      ./build_test/tests/cw_decoder/hist_forget_sweep
add_executable(hist_forget_sweep tools/hist_forget_sweep.cpp)
target_include_directories(hist_forget_sweep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hist_forget_sweep PRIVATE cw_decoder)

# Offline WAV inspector (not a test): reads a real 4000 Hz recording, runs the
# real CwReceiver and writes decoded text plus an SVG timeline. The include dir
# gives it the test-only diag access headers.
# Run: cmake --build build_test --target cw_wav_inspect &&
#      ./build_test/tests/cw_decoder/cw_wav_inspect <file.wav> <threshold_db>
add_executable(cw_wav_inspect tools/cw_wav_inspect.cpp)
target_include_directories(cw_wav_inspect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cw_wav_inspect PRIVATE cw_decoder)