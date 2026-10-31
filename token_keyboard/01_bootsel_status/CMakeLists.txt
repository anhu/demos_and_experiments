add_executable(01_bootsel_status bootsel_status.c)

target_compile_options(01_bootsel_status PRIVATE ${BOOTSEL_WARNINGS})
target_link_libraries(01_bootsel_status PRIVATE pico_stdlib bootsel_common)

set_target_properties(01_bootsel_status PROPERTIES OUTPUT_NAME 01_bootsel_status)
pico_set_program_name(01_bootsel_status "01_bootsel_status")
pico_set_program_description(01_bootsel_status "Image 1: report BOOTSEL button state over USB serial")

# Output goes over USB CDC (a virtual serial port), not the UART pins.
pico_enable_stdio_usb(01_bootsel_status 1)
pico_enable_stdio_uart(01_bootsel_status 0)

pico_add_extra_outputs(01_bootsel_status)
bootsel_collect_uf2(01_bootsel_status)
