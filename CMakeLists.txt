cmake_minimum_required(VERSION 3.13)

# Target: RP2350 Pico 2
set(PICO_BOARD pico2 CACHE STRING "Pico board type")

# This firmware is M2-only.
set(BOARD_VARIANT "M2" CACHE STRING "Board variant (M2)")

set(CPU_SPEED "252" CACHE STRING "CPU clock in MHz")

# When OFF, the native USB port is reserved for CDC stdio (debug console).
# When ON, TinyUSB Host claims the native USB port and gamepads can be read
# — CDC stdio is disabled in that configuration.
option(USB_HID_ENABLED "Enable USB HID host for gamepad capture" OFF)

include(pico_sdk_import.cmake)

project(frank-gamepad C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(src/fatfs)
add_subdirectory(drivers/sdcard)
add_subdirectory(drivers/usbhid)

# HDMI driver library
add_library(hdmi_driver STATIC
    drivers/HDMI.c
    drivers/hdmi_scanline.S
)
target_include_directories(hdmi_driver PUBLIC drivers src)
target_compile_definitions(hdmi_driver PUBLIC BOARD_${BOARD_VARIANT})
target_link_libraries(hdmi_driver pico_stdlib hardware_dma hardware_pio hardware_gpio)

add_executable(frank-gamepad
    src/main.c
    src/ui.c
)

target_include_directories(frank-gamepad PRIVATE
    src
    src/fatfs
    drivers
    drivers/sdcard
)

if(USB_HID_ENABLED)
    # Only expose the usbhid headers (and its tusb_config.h) in release builds.
    # Debug builds use pico_stdio_usb which ships its own tusb_config.
    target_include_directories(frank-gamepad PRIVATE drivers/usbhid)
endif()

target_compile_definitions(frank-gamepad PRIVATE
    BOARD_${BOARD_VARIANT}
    CPU_CLOCK_MHZ=${CPU_SPEED}
    PICO_ON_DEVICE=1
)

# Peripheral pin overrides for M2.
target_compile_definitions(frank-gamepad PRIVATE
    SDCARD_PIN_SPI0_CS=5
    SDCARD_PIN_SPI0_SCK=6
    SDCARD_PIN_SPI0_MOSI=7
    SDCARD_PIN_SPI0_MISO=4
)

target_link_libraries(frank-gamepad
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_dma
    hardware_pio
    hardware_gpio
    hardware_clocks
    sdcard
    fatfs
    hdmi_driver
    usbhid
)

if(USB_HID_ENABLED)
    message(STATUS "USB HID host ENABLED (CDC stdio disabled; UART only)")
    target_compile_definitions(frank-gamepad PRIVATE USB_HID_ENABLED=1)
    pico_enable_stdio_uart(frank-gamepad 1)
    pico_enable_stdio_usb(frank-gamepad 0)
else()
    message(STATUS "USB HID host DISABLED - native USB reserved for CDC stdio (debug build)")
    pico_enable_stdio_uart(frank-gamepad 0)
    pico_enable_stdio_usb(frank-gamepad 1)
endif()

pico_add_extra_outputs(frank-gamepad)
