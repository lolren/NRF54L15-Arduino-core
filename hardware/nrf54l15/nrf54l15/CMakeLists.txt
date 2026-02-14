# CMake Build Configuration for nRF54L15 Zephyr-Based Arduino Core
# Licensed under the Apache License 2.0

cmake_minimum_required(VERSION 3.20)
project(nrf54l15_arduino C CXX ASM)

# Set the target processor
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM Cortex-M33)

# Toolchain paths (adjust to your installation)
set(TOOLCHAIN_PREFIX arm-none-eabi-)

# Cross-compilation settings
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)

# CPU flags
set(CPU_FLAGS
    -mcpu=cortex-m33
    -mthumb
    -mfloat-abi=soft
)

# Compiler options
add_compile_options(
    ${CPU_FLAGS}
    -Wall
    -Wextra
    -O2
    -g3
    -ffunction-sections
    -fdata-sections
)

# C flags
add_compile_options(
    $<$<COMPILE_LANGUAGE:C>:-std=gnu17>
)

# C++ flags
add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
)

# Linker flags
add_link_options(
    ${CPU_FLAGS}
    -Wl,--gc-sections
    -Wl,-Map,$<TARGET_FILE_BASE:${PROJECT_NAME}>.map
    -Wl,--print-memory-usage
)

# Defines
add_definitions(
    -DF_CPU=64000000UL
    -DARDUINO=10819
    -DARDUINO_NRF54L15
)

# Include directories
include_directories(
    cores/nrf54l15
    variants/xiao_nrf54l15
)

# Core library sources
set(CORE_SOURCES
    cores/nrf54l15/Print.cpp
    cores/nrf54l15/HardwareSerial.cpp
    cores/nrf54l15/SPI.cpp
    cores/nrf54l15/Wire.cpp
    cores/nrf54l15/main.cpp
    cores/nrf54l15/wiring_digital.c
    cores/nrf54l15/wiring_time.c
    cores/nrf54l15/wiring_math.c
    cores/nrf54l15/wiring_random.c
    cores/nrf54l15/wiring_analog.c
)

# Variant sources
set(VARIANT_SOURCES
    variants/xiao_nrf54l15/variant.cpp
)

# Core library
add_library(arduino_core STATIC ${CORE_SOURCES} ${VARIANT_SOURCES})

target_include_directories(arduino_core PUBLIC
    cores/nrf54l15
    variants/xiao_nrf54l15
)

# Example sketch function (call with: make -C build sketch SKETCH=examples/01.Basics/Blink)
function(add_sketch SKETCH_DIR)
    get_filename_component(SKETCH_NAME ${SKETCH_DIR} NAME_WE)
    get_filename_component(SKETCH_FILE ${SKETCH_DIR} ABSOLUTE)

    # Get all .ino and .cpp files in the sketch directory
    file(GLOB_RECURSE SKETCH_SOURCES
        "${SKETCH_FILE}/*.ino"
        "${SKETCH_FILE}/*.cpp"
        "${SKETCH_FILE}/*.c"
    )

    # Create sketch executable
    add_executable(${SKETCH_NAME} ${SKETCH_SOURCES})
    target_link_libraries(${SKETCH_NAME} arduino_core)

    # Create hex and bin files
    add_custom_command(TARGET ${SKETCH_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${SKETCH_NAME}> ${SKETCH_NAME}.hex
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${SKETCH_NAME}> ${SKETCH_NAME}.bin
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${SKETCH_NAME}>
        COMMENT "Building ${SKETCH_NAME}.hex and ${SKETCH_NAME}.bin"
    )
endfunction()
