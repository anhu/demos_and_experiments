add_executable(04_click_phrases
        click_phrases.c
        usb_descriptors.c
        )

target_compile_options(04_click_phrases PRIVATE ${BOOTSEL_WARNINGS})

# So TinyUSB finds our tusb_config.h (and the code finds phrases.h).
target_include_directories(04_click_phrases PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(04_click_phrases PRIVATE
        pico_stdlib
        pico_unique_id       # chip serial number for the USB serial string
        tinyusb_device       # USB device stack
        bootsel_common       # bootsel_pressed() + board_config.h
        )

target_compile_definitions(04_click_phrases PRIVATE CFG_TUSB_OS=OPT_OS_PICO)

set_target_properties(04_click_phrases PROPERTIES OUTPUT_NAME 04_click_phrases)
pico_set_program_name(04_click_phrases "04_click_phrases")
pico_set_program_description(04_click_phrases "Image 4: N clicks of BOOTSEL types phrase N as a USB keyboard")

# NO stdio over USB here: a CDC interface would clash with our HID descriptors.
pico_enable_stdio_usb(04_click_phrases 0)
pico_enable_stdio_uart(04_click_phrases 0)

pico_add_extra_outputs(04_click_phrases)
bootsel_collect_uf2(04_click_phrases)
