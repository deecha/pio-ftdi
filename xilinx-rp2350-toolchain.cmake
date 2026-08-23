# xilinx-arm-rp2350.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(VITIS_ARM
    /opt/Xilinx/Vitis/2023.2/gnu/aarch32/lin/gcc-arm-none-eabi
)

set(CMAKE_C_COMPILER
    ${VITIS_ARM}/bin/arm-none-eabi-gcc
)

set(CMAKE_CXX_COMPILER
    ${VITIS_ARM}/bin/arm-none-eabi-g++
)

set(CMAKE_ASM_COMPILER
    ${VITIS_ARM}/bin/arm-none-eabi-gcc
)

set(CMAKE_AR
    ${VITIS_ARM}/bin/arm-none-eabi-ar
)

set(CMAKE_RANLIB
    ${VITIS_ARM}/bin/arm-none-eabi-ranlib
)

set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-m33 -mthumb"
)

set(CMAKE_CXX_FLAGS_INIT
    "-mcpu=cortex-m33 -mthumb"
)

set(CMAKE_ASM_FLAGS_INIT
    "-mcpu=cortex-m33 -mthumb"
)

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-mcpu=cortex-m33 -mthumb"
)
