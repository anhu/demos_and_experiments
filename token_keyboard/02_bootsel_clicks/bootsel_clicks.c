/* Image 2: 02_bootsel_clicks
 *
 * Counts consecutive clicks of the BOOTSEL button and prints the count over
 * USB serial. A click is one press followed by one release. When no new
 * press arrives within CLICK_TIMEOUT_MS after the last release, the
 * sequence is over: "clicks: N" is printed and the counter resets.
 *
 * Timing is done with to_ms_since_boot(); the only sleep is the 5 ms poll
 * pacing at the top of the loop.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "board_config.h"
#include "bootsel.h"

/* ---- tuning constants ------------------------------------------------- */
#define POLL_INTERVAL_MS     5   /* how often the button is sampled           */
#define DEBOUNCE_MS         20   /* raw reading must be stable this long      */
#define CLICK_TIMEOUT_MS   400   /* silence after a release that ends a burst */
#define LED_BLINK_MS        60   /* LED flash per accepted click (optional)   */
#define MAX_TIMED_CLICKS    16   /* per-sequence timing records kept          */

/* Per-click timing, printed after each sequence to help tune the constants. */
typedef struct {
    uint32_t gap_ms;    /* time from previous release to this press (0 for 1st) */
    uint32_t press_ms;  /* how long the button was held                         */
} click_timing_t;

int main(void)
{
    stdio_init_all();
    board_led_init();

    printf("\n02_bootsel_clicks: poll %d ms, debounce %d ms, inter-click timeout %d ms\n",
           POLL_INTERVAL_MS, DEBOUNCE_MS, CLICK_TIMEOUT_MS);

    /* debounce state */
    bool     raw_last   = bootsel_pressed();
    bool     stable     = raw_last;           /* debounced level */
    uint32_t raw_since  = to_ms_since_boot(get_absolute_time());

    /* click-sequence state */
    unsigned       clicks       = 0;
    uint32_t       press_time   = 0;
    uint32_t       release_time = 0;
    click_timing_t timing[MAX_TIMED_CLICKS];

    /* LED state */
    uint32_t led_off_at = 0;
    bool     led_on     = false;

    absolute_time_t next_poll = make_timeout_time_ms(POLL_INTERVAL_MS);

    while (true) {
        sleep_until(next_poll);
        next_poll = delayed_by_ms(next_poll, POLL_INTERVAL_MS);

        uint32_t now = to_ms_since_boot(get_absolute_time());
        bool raw = bootsel_pressed();

        /* ---- debounce: accept a new level only after DEBOUNCE_MS of stability */
        if (raw != raw_last) {
            raw_last  = raw;
            raw_since = now;
        } else if (raw != stable && (now - raw_since) >= DEBOUNCE_MS) {
            stable = raw;
            if (stable) {
                /* accepted PRESS */
                if (clicks < MAX_TIMED_CLICKS) {
                    timing[clicks].gap_ms = clicks ? (now - release_time) : 0;
                }
                press_time = now;
                clicks++;
                board_led_set(true);
                led_on     = true;
                led_off_at = now + LED_BLINK_MS;
            } else {
                /* accepted RELEASE: this completes click number `clicks` */
                release_time = now;
                if (clicks && clicks <= MAX_TIMED_CLICKS) {
                    timing[clicks - 1].press_ms = now - press_time;
                }
            }
        }

        /* ---- end of sequence: released, and quiet for CLICK_TIMEOUT_MS */
        if (clicks && !stable && (now - release_time) >= CLICK_TIMEOUT_MS) {
            printf("clicks: %u\n", clicks);
            printf("  timing:");
            unsigned shown = clicks < MAX_TIMED_CLICKS ? clicks : MAX_TIMED_CLICKS;
            for (unsigned i = 0; i < shown; ++i) {
                printf(" [%u] press %lu ms", i + 1, (unsigned long)timing[i].press_ms);
                if (i) printf(" after %lu ms gap", (unsigned long)timing[i].gap_ms);
                if (i + 1 < shown) printf(",");
            }
            if (clicks > MAX_TIMED_CLICKS) printf(" ... (%u more)", clicks - MAX_TIMED_CLICKS);
            printf("\n");
            clicks = 0;
        }

        /* ---- LED: short flash per accepted click */
        if (led_on && (int32_t)(now - led_off_at) >= 0) {
            board_led_set(false);
            led_on = false;
        }
    }
}
