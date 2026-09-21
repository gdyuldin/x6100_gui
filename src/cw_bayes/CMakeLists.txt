# CW Bayesian decoder module: standalone C++17 module, standard library only.
# Not part of the application build yet (src/CMakeLists.txt is untouched); it is
# compiled only from the test tree.
add_library(cw_bayes STATIC
    power_detector.cpp
    time_classifier.cpp
    morse_decoder.cpp
    cw_receiver.cpp
)

target_compile_features(cw_bayes PUBLIC cxx_std_17)

target_include_directories(cw_bayes PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)