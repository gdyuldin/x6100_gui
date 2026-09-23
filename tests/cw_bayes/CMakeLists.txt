file(GLOB TEST_SOURCES "*.cpp")
add_executable(test_cw_bayes ${TEST_SOURCES})
target_link_libraries(test_cw_bayes PRIVATE cw_bayes Catch2::Catch2WithMain)
add_test(NAME test_cw_bayes COMMAND $<TARGET_FILE:test_cw_bayes> --colour-mode=ansi)

# Measurement tool (not a test): only built, never run by ctest, so `ctest`
# stays fast. The glob above does not descend into tools/, hence its own target.
# Run: cmake --build build_test --target hist_forget_sweep &&
#      ./build_test/tests/cw_bayes/hist_forget_sweep
add_executable(hist_forget_sweep tools/hist_forget_sweep.cpp)
target_include_directories(hist_forget_sweep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hist_forget_sweep PRIVATE cw_bayes)