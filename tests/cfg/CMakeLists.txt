add_subdirectory(../../src/cfg ${CMAKE_BINARY_DIR}/src/cfg)
file(GLOB TEST_SOURCES "*.cpp")
add_executable(test_cfg ${TEST_SOURCES})
target_link_libraries(test_cfg PRIVATE cfg Catch2::Catch2WithMain)

# The cfg tests deliberately exercise the deprecated C API (cfg_api.h /
# subject_api.h) to verify their behaviour, so silence the expected
# -Wdeprecated-declarations warnings for this test binary.
target_compile_options(test_cfg PRIVATE -Wno-deprecated-declarations)
add_test(NAME test_cfg COMMAND $<TARGET_FILE:test_cfg> --colour-mode=ansi)

