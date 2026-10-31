add_executable(02_bootsel_clicks bootsel_clicks.c)

target_compile_options(02_bootsel_clicks PRIVATE ${BOOTSEL_WARNINGS})
target_link_libraries(02_bootsel_clicks PRIVATE pico_stdlib bootsel_common)

set_target_properties(02_bootsel_clicks PROPERTIES OUTPUT_NAME 02_bootsel_clicks)
pico_set_program_name(02_bootsel_clicks "02_bootsel_clicks")
pico_set_program_description(02_bootsel_clicks "Image 2: count consecutive BOOTSEL clicks, print over USB serial")

pico_enable_stdio_usb(02_bootsel_clicks 1)
pico_enable_stdio_uart(02_bootsel_clicks 0)

pico_add_extra_outputs(02_bootsel_clicks)
bootsel_collect_uf2(02_bootsel_clicks)
