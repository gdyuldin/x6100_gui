#!/bin/bash

cmake -B build_test/ -DENABLE_TESTING=YES -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && \
cmake --build build_test/ --target all -j10 && \
cd build_test/ && ctest --output-on-failure


