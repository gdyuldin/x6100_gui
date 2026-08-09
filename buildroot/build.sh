#!/bin/bash

source ./conf.sh

PATH=../${BUILDROOT}/host/bin:${PATH}

mkdir build

cd build


cmake \
    ../../ \
    -DCMAKE_BUILD_TYPE:STRING=Release \
    -DCMAKE_TOOLCHAIN_FILE=../${BUILDROOT}/host/share/buildroot/toolchainfile.cmake \
    -DCMAKE_INSTALL_PREFIX="/usr" \
    -DCMAKE_INSTALL_RUNSTATEDIR="/run" \
    -DCMAKE_COLOR_MAKEFILE=OFF \
    -DBUILD_DOC=OFF \
    -DBUILD_DOCS=OFF \
    -DBUILD_EXAMPLE=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TEST=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_TESTING=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_SHARED_LIB=OFF \
    -DLV_CONF_BUILD_DISABLE_EXAMPLES=ON \
    -DLV_CONF_BUILD_DISABLE_DEMOS=ON

make -j$((`nproc`))

cd -
