/* board_config.h -- the one place that knows which pin the LED is on.
 *
 * Board: USB-A token with an RP2350 (Raspberry Pi Pico 2 reference design,
 * "Feitian RP2350" in the Bruce firmware). Built with -DPICO_BOARD=pico2.
 *
 * LED: GP25, ACTIVE-HIGH (drive high to light). Source: Bruce's board
 * profile, which cites the token's schematic (GP25 -> 510 ohm -> two green
 * LEDs -> GND). Status: CONFIRMED on hardware 2026-10-30 (image 0 blinks the
 * green LED at 1 Hz with these values). Chip: RP2350 rev A4, QFN60 package.
 * If a different token ever needs other values, change the two defines
 * below and rebuild; every image picks them up from here.
 *
 * The BOOTSEL button needs no pin here: it sits on the flash chip-select
 * line (QSPI_SS), not a GPIO. See bootsel.h.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdbool.h>
#include "hardware/gpio.h"

#define BOARD_LED_PIN         25
#define BOARD_LED_ACTIVE_HIGH 1   /* 1 = drive high to light, 0 = drive low */

/* Plain GPIO helpers, deliberately no LED library and no PICO_DEFAULT_LED_PIN. */
static inline void board_led_init(void)
{
    gpio_init(BOARD_LED_PIN);
    gpio_set_dir(BOARD_LED_PIN, GPIO_OUT);
    gpio_put(BOARD_LED_PIN, !BOARD_LED_ACTIVE_HIGH); /* start OFF */
}

static inline void board_led_set(bool on)
{
    gpio_put(BOARD_LED_PIN, BOARD_LED_ACTIVE_HIGH ? on : !on);
}

#endif /* BOARD_CONFIG_H */
