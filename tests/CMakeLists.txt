cmake_minimum_required(VERSION 3.23)

# add_executable(tests test.cpp)
# target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

add_compile_options(-fsanitize=address -fsanitize=undefined  -fno-omit-frame-pointer -fno-sanitize-recover)
add_link_options(-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -fno-sanitize-recover -static-libasan -static-libubsan)


add_subdirectory(../src/qth ${CMAKE_BINARY_DIR}/src/qth)
add_executable(test_qth test_qth.cpp)
target_link_libraries(test_qth PRIVATE QTH Catch2::Catch2WithMain)
add_test(NAME test_qth COMMAND $<TARGET_FILE:test_qth> --colour-mode=ansi )

add_subdirectory(../src/ft8 ${CMAKE_BINARY_DIR}/src/ft8)
add_executable(test_ft8_qso test_ft8_qso.cpp)
target_link_libraries(test_ft8_qso PRIVATE FT8 QTH aether_x6100_control Catch2::Catch2WithMain)
add_test(NAME test_ft8_qso COMMAND $<TARGET_FILE:test_ft8_qso> --colour-mode=ansi )


add_subdirectory(cfg)
