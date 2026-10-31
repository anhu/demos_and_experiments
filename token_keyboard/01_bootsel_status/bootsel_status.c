/* Image 1: 01_bootsel_status
 *
 * Polls the BOOTSEL button every 10 ms and reports over USB serial:
 *   - a line on every change:  "BOOTSEL: pressed" / "BOOTSEL: released"
 *   - a heartbeat once a second with the current state
 * The LED mirrors the button (on while pressed).
 *
 * This image exists to prove bootsel_pressed() works on this chip. If the
 * state never changes when you press the button, stop here and report it.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "board_config.h"
#include "bootsel.h"

#define POLL_INTERVAL_MS   10
#define HEARTBEAT_MS     1000

int main(void)
{
    stdio_init_all();
    board_led_init();

    /* Single-core by design: bootsel_pressed() floats the flash chip-select
     * for a few microseconds, and core 1 must not be fetching from flash
     * while that happens. We simply never start core 1. */

    bool last = bootsel_pressed();
    board_led_set(last);

    uint32_t last_heartbeat = to_ms_since_boot(get_absolute_time());
    absolute_time_t next_poll = make_timeout_time_ms(POLL_INTERVAL_MS);

    printf("\n01_bootsel_status: polling BOOTSEL every %d ms (heartbeat every %d ms)\n",
           POLL_INTERVAL_MS, HEARTBEAT_MS);
    printf("BOOTSEL: %s (initial)\n", last ? "pressed" : "released");

    while (true) {
        sleep_until(next_poll);
        next_poll = delayed_by_ms(next_poll, POLL_INTERVAL_MS);

        bool pressed = bootsel_pressed();
        if (pressed != last) {
            last = pressed;
            board_led_set(pressed);
            printf("BOOTSEL: %s\n", pressed ? "pressed" : "released");
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_heartbeat >= HEARTBEAT_MS) {
            last_heartbeat += HEARTBEAT_MS;
            printf("heartbeat t=%lus  BOOTSEL is %s\n",
                   (unsigned long)(now / 1000), pressed ? "pressed" : "released");
        }
    }
}
