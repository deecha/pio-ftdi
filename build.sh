#!/bin/bash
BUILDDIR=_build
rm -rf ${BUILDDIR}
export PICO_SDK_PATH=$HOME/build/pico-sdk
cp -f $PICO_SDK_PATH/external/pico_sdk_import.cmake .
cmake -DSINGLE_CHANNEL=1 -DCMAKE_TOOLCHAIN_FILE=./xilinx-rp2350-toolchain.cmake -B ${BUILDDIR} -S .
cmake --build ${BUILDDIR}
