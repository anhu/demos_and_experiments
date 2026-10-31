add_executable(00_led_blink led_blink.c)

target_compile_options(00_led_blink PRIVATE ${BOOTSEL_WARNINGS})
target_link_libraries(00_led_blink PRIVATE pico_stdlib hardware_gpio bootsel_common)

pico_set_program_name(00_led_blink "00_led_blink")
pico_set_program_description(00_led_blink "Image 0: blink the LED (GP25 active-high) or scan for it")

if (LED_SCAN)
    # Pin-scan mode: step through candidate GPIOs and report over USB serial.
    target_compile_definitions(00_led_blink PRIVATE LED_SCAN=1)
    pico_enable_stdio_usb(00_led_blink 1)
    pico_enable_stdio_uart(00_led_blink 0)
    set_target_properties(00_led_blink PROPERTIES OUTPUT_NAME 00_led_blink_scan)
    message(STATUS "00_led_blink: LED_SCAN=ON -> builds 00_led_blink_scan.uf2")
else()
    # Plain blink: no stdio at all, keep the image as simple as possible.
    set_target_properties(00_led_blink PROPERTIES OUTPUT_NAME 00_led_blink)
    pico_enable_stdio_usb(00_led_blink 0)
    pico_enable_stdio_uart(00_led_blink 0)
endif()

pico_add_extra_outputs(00_led_blink)   # .uf2 / .bin / .hex / .map
bootsel_collect_uf2(00_led_blink)    # copy the .uf2 into out/
