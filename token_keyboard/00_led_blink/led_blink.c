/* Image 0: 00_led_blink
 *
 * Step A (default build): blink the LED named in board_config.h
 * (GP25, active-high) on for 500 ms, off for 500 ms, forever.
 *
 * Step B (-DLED_SCAN=ON): if nothing blinks, walk every candidate GPIO,
 * trying both polarities, and say over USB serial which pin is being driven
 * so you can report which one lights the LED.
 *
 * Plain gpio_init / gpio_set_dir / gpio_put only. No LED library, no
 * PICO_DEFAULT_LED_PIN, no CYW43 (this chip has no wireless).
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "board_config.h"

#ifndef LED_SCAN
#define LED_SCAN 0
#endif

#if !LED_SCAN

/* ---------------------------------------------------------------------- */
/* Step A: plain blink                                                     */
/* ---------------------------------------------------------------------- */
int main(void)
{
    board_led_init();
    while (true) {
        board_led_set(true);
        sleep_ms(500);
        board_led_set(false);
        sleep_ms(500);
    }
}

#else /* LED_SCAN */

/* ---------------------------------------------------------------------- */
/* Step B: pin scan                                                        */
/* ---------------------------------------------------------------------- */
#include <stdio.h>
#include "pico/stdio_usb.h"

/* Candidate pins: GP0..GP29 except the three the Pico 2 reference design
 * uses for power-supply mode (GP23), USB VBUS sense (GP24) and VSYS sense
 * (GP29). Those are never driven. */
static bool pin_is_candidate(uint pin)
{
    return pin != 23 && pin != 24 && pin != 29;
}

/* How long each (pin, polarity) test lasts, and how fast it blinks. */
#define PHASE_MS      2000
#define BLINK_HALF_MS  250

/* Test one polarity on one pin. The "on" level is driven for BLINK_HALF_MS,
 * then the pin is released to a plain input (high-impedance, pulls off)
 * for BLINK_HALF_MS, and so on for PHASE_MS. An LED wired to this pin
 * blinks ONLY if its polarity matches: in the high-impedance half-cycles
 * no current flows whichever way the LED is wired. So "blinks during the
 * active-high phase" means active-high, and vice versa.
 */
static void test_pin(uint pin, bool active_high)
{
    printf("GP%-2u  %-11s : driving for %d ms ... look at the LED\n",
           pin, active_high ? "active-HIGH" : "active-LOW", PHASE_MS);

    gpio_init(pin);                   /* SIO function, input, output latch 0 */
    gpio_disable_pulls(pin);          /* so a Hi-Z pin cannot faintly light anything */

    for (int t = 0; t < PHASE_MS; t += 2 * BLINK_HALF_MS) {
        gpio_put(pin, active_high);   /* drive the "on" level ... */
        gpio_set_dir(pin, GPIO_OUT);
        sleep_ms(BLINK_HALF_MS);
        gpio_set_dir(pin, GPIO_IN);   /* ... then let go */
        sleep_ms(BLINK_HALF_MS);
    }

    /* Return the pin to a plain input before moving on. */
    gpio_set_dir(pin, GPIO_IN);
    gpio_set_pulls(pin, false, true); /* chip reset default: pull-down */
    gpio_deinit(pin);
}

int main(void)
{
    stdio_init_all();

    /* Give the user a moment to open the serial terminal, then start.
     * The scan repeats forever, so missing the first pass is harmless. */
    for (int i = 0; i < 100 && !stdio_usb_connected(); ++i)
        sleep_ms(100);

    for (unsigned pass = 1;; ++pass) {
        printf("\n=== LED pin scan, pass %u ===\n", pass);
        printf("Candidates: GP0..GP29 except GP23, GP24, GP29.\n");
        printf("Each pin is tested active-HIGH then active-LOW, %d ms each.\n",
               PHASE_MS);
        printf("Note the pin and polarity line that is printed while the LED blinks.\n\n");

        for (uint pin = 0; pin < 30; ++pin) {
            if (!pin_is_candidate(pin))
                continue;
            test_pin(pin, true);      /* only ever one pin driven at a time */
            test_pin(pin, false);
        }
        printf("\n=== pass %u done; starting again ===\n", pass);
    }
}

#endif /* LED_SCAN */
