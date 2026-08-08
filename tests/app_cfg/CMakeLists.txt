

# add_executable(test_subject test_subject.cpp)
# target_link_libraries(test_subject PRIVATE app_cfg Catch2::Catch2WithMain)
# add_test(NAME test_subject COMMAND $<TARGET_FILE:test_subject> --colour-mode=ansi )


# add_executable(test_subject test_subject.cpp)
# target_link_libraries(test_subject PRIVATE app_cfg Catch2::Catch2WithMain)
# add_test(NAME test_subject COMMAND $<TARGET_FILE:test_subject> --colour-mode=ansi )


file(GLOB TEST_SOURCES "*.cpp")
add_executable(test_app_cfg ${TEST_SOURCES})
target_link_libraries(test_app_cfg PRIVATE app_cfg Catch2::Catch2WithMain)
add_test(NAME test_app_cfg COMMAND $<TARGET_FILE:test_app_cfg> --colour-mode=ansi)
# include(CTest)
# catch_discover_tests(test_app_cfg)
