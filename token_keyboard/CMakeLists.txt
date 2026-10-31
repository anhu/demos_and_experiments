# bootsel-experiments: four small RP2350 firmware images (see README.md).
#
# Configure + build (from this directory):
#   cmake -S . -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   cmake --build build
# The UF2 files land in build/<image>/<image>.uf2.
#
# Image 0 in LED pin-scan mode (separate build directory):
#   cmake -S . -B build-scan -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk -DLED_SCAN=ON
#   cmake --build build-scan --target 00_led_blink
cmake_minimum_required(VERSION 3.13)

# The token has no board file in the SDK. It follows the Raspberry Pi Pico 2
# reference design, so use the stock pico2 target (RP2350, Arm, secure).
# Both must be set BEFORE the SDK import below.
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

# Flash size. The pico2 board header declares 4 MiB, but Bruce's C firmware
# MEASURED 2 MiB on this token (a write at 0x200000 read back at 0x000000, and
# `picotool erase -a` reported 2097152 bytes). The boot ROM cannot tell us
# (OTP FLASH_DEVINFO is unprogrammed), so declare the measured size: it bounds
# the linker's FLASH region to the real chip. These images are < 64 KiB anyway.
set(PICO_FLASH_SIZE_BYTES 2097152)

# Pull in the SDK (must be before project()).
include(pico_sdk_import.cmake)

project(bootsel_experiments C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "2.0.0")
    message(FATAL_ERROR "pico-sdk 2.0.0 or later required (found ${PICO_SDK_VERSION_STRING})")
endif()

# Initialise the SDK (sets up the cross-compiler, boot ROM glue, picotool...).
pico_sdk_init()

option(LED_SCAN "Build image 0 in LED pin-scan mode (prints over USB serial)" OFF)

# Warnings for OUR sources only (the SDK and TinyUSB keep their own settings).
set(BOOTSEL_WARNINGS -Wall -Wextra)

# Shared code: LED pin/polarity (board_config.h) and the BOOTSEL reader.
add_library(bootsel_common INTERFACE)
target_sources(bootsel_common INTERFACE ${CMAKE_CURRENT_LIST_DIR}/common/bootsel.c)
target_include_directories(bootsel_common INTERFACE ${CMAKE_CURRENT_LIST_DIR}/common)
target_link_libraries(bootsel_common INTERFACE pico_stdlib hardware_gpio hardware_sync)

# After each image is built, copy its .uf2 into <project>/out/ so all images
# to flash sit in one flat folder. Call AFTER pico_add_extra_outputs().
set(BOOTSEL_OUT_DIR ${CMAKE_CURRENT_LIST_DIR}/out)
function(bootsel_collect_uf2 target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BOOTSEL_OUT_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy
                $<TARGET_FILE_DIR:${target}>/$<TARGET_PROPERTY:${target},OUTPUT_NAME>.uf2
                ${BOOTSEL_OUT_DIR}/
        COMMENT "Copying $<TARGET_PROPERTY:${target},OUTPUT_NAME>.uf2 to out/"
        VERBATIM)
endfunction()

add_subdirectory(00_led_blink)
add_subdirectory(01_bootsel_status)
add_subdirectory(02_bootsel_clicks)
add_subdirectory(03_keyboard_alphabet)
add_subdirectory(04_click_phrases)
