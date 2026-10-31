add_executable(03_keyboard_alphabet
        keyboard_alphabet.c
        usb_descriptors.c
        )

target_compile_options(03_keyboard_alphabet PRIVATE ${BOOTSEL_WARNINGS})

# So TinyUSB finds our tusb_config.h.
target_include_directories(03_keyboard_alphabet PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# tinyusb_device = the USB device stack; pico_unique_id = chip serial number
# used for the USB serial-number string. No tinyusb_board (BSP) needed.
target_link_libraries(03_keyboard_alphabet PRIVATE
        pico_stdlib
        pico_unique_id
        tinyusb_device
        bootsel_common       # only for board_config.h (LED); BOOTSEL is not read here
        )

target_compile_definitions(03_keyboard_alphabet PRIVATE CFG_TUSB_OS=OPT_OS_PICO)

set_target_properties(03_keyboard_alphabet PROPERTIES OUTPUT_NAME 03_keyboard_alphabet)
pico_set_program_name(03_keyboard_alphabet "03_keyboard_alphabet")
pico_set_program_description(03_keyboard_alphabet "Image 3: USB HID keyboard that types a..z, one letter per second")

# NO stdio over USB here: a CDC interface would clash with our HID descriptors.
pico_enable_stdio_usb(03_keyboard_alphabet 0)
pico_enable_stdio_uart(03_keyboard_alphabet 0)

pico_add_extra_outputs(03_keyboard_alphabet)
bootsel_collect_uf2(03_keyboard_alphabet)
