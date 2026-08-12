
file(GLOB TEST_SOURCES "*.cpp")
add_executable(test_cfg ${TEST_SOURCES})
target_link_libraries(test_cfg PRIVATE cfg Catch2::Catch2WithMain)
add_test(NAME test_cfg COMMAND $<TARGET_FILE:test_cfg> --colour-mode=ansi)

