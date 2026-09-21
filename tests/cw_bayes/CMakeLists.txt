file(GLOB TEST_SOURCES "*.cpp")
add_executable(test_cw_bayes ${TEST_SOURCES})
target_link_libraries(test_cw_bayes PRIVATE cw_bayes Catch2::Catch2WithMain)
add_test(NAME test_cw_bayes COMMAND $<TARGET_FILE:test_cw_bayes> --colour-mode=ansi)