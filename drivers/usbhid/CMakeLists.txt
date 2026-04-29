# USB HID host library for frank-gamepad
# When USB_HID_ENABLED is off, this is an empty interface so main.c can build
# against USB stubs in usbhid.h without pulling TinyUSB Host in.
# SPDX-License-Identifier: MIT

add_library(usbhid INTERFACE)

if(USB_HID_ENABLED)
    message(STATUS "USB HID host: compiling hid_app.c")

    target_sources(usbhid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/hid_app.c
    )

    target_include_directories(usbhid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_compile_definitions(usbhid INTERFACE
        USB_HID_ENABLED=1
    )

    target_link_libraries(usbhid INTERFACE
        tinyusb_host
        tinyusb_board
    )
else()
    # No sources, no tinyusb link, NO include dir exposure — tusb_config.h
    # in this directory would otherwise be picked up by the SDK's CDC build
    # and disable device mode, breaking pico_stdio_usb.
endif()
