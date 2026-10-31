/* Image 4: 04_click_phrases
 *
 * Image 2 (BOOTSEL click counting) + image 3 (USB HID keyboard): click the
 * button N times and, CLICK_TIMEOUT_MS after the last release, the token
 * types phrase N from phrases.h into whatever window has keyboard focus.
 *
 * Behaviour (as requested 2026-10-31):
 *   - 1..PHRASE_COUNT clicks: the matching phrase is typed. No Enter at the
 *     end.
 *   - any other count, or clicks while not connected to a host: nothing.
 *   - LED: ON while a phrase is being typed, OFF at all other times. No idle
 *     blink, no flash per click.
 *   - button presses while a phrase is being typed are ignored; a phrase
 *     always runs to its end. Unplugging the token is the only way to stop it.
 *   - on (re)connect the state is reset.
 *
 * Everything is non-blocking: every loop iteration runs tud_task(), the
 * 5 ms button poll, the typing state machine and the LED. The BOOTSEL sample
 * masks interrupts for a few tens of microseconds, which TinyUSB tolerates.
 *
 * Per character: one key-down report (with Left Shift for capitals and
 * shifted symbols), then one all-keys-up report, then CHAR_GAP_MS. Each
 * report waits for the host's interrupt-endpoint poll (HID_POLL_INTERVAL_MS
 * in usb_descriptors.c), so a character costs about 2 polls + the gap.
 *
 * Keyboard code started from pico-examples usb/device/dev_hid_composite
 * (The MIT License, Copyright (c) 2019 Ha Thach, tinyusb.org); the
 * ASCII-to-keycode table is TinyUSB's HID_ASCII_TO_KEYCODE (US layout).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "board_config.h"
#include "bootsel.h"
#include "usb_descriptors.h"
#include "phrases.h"

/* ---- tuning constants ------------------------------------------------- */
#define POLL_INTERVAL_MS     5   /* button sample period                       */
#define DEBOUNCE_MS         20   /* raw reading must be stable this long       */
#define CLICK_TIMEOUT_MS   400   /* silence after a release that ends a burst  */
#define CHAR_GAP_MS          5   /* extra pause after each character's key-up  */

static inline uint32_t millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/* TinyUSB 0.18 "API implemented by user"; the RP2 port does not call it. */
uint32_t tusb_time_millis_api(void)
{
    return millis();
}

/* ASCII -> { needs shift, HID keycode }, US layout, supplied by TinyUSB. */
static uint8_t const conv_table[128][2] = { HID_ASCII_TO_KEYCODE };

/* --------------------------------------------------------------------- */
/* Typing state machine                                                   */
/* --------------------------------------------------------------------- */
enum type_state { TYPE_IDLE, TYPE_SEND_DOWN, TYPE_SEND_UP, TYPE_GAP };

static enum type_state type_state     = TYPE_IDLE;
static const char     *type_text      = NULL;   /* phrase being typed      */
static size_t          type_pos       = 0;      /* index of next character */
static uint32_t        type_gap_until = 0;

static inline bool typing_active(void)
{
    return type_state != TYPE_IDLE;
}

static void typing_start(const char *text)
{
    type_text  = text;
    type_pos   = 0;
    type_state = TYPE_SEND_DOWN;
}

static void typing_reset(void)
{
    type_state = TYPE_IDLE;
}

static void typing_task(void)
{
    switch (type_state) {
    case TYPE_IDLE:
        return;

    case TYPE_GAP:
        if ((int32_t)(millis() - type_gap_until) < 0)
            return;                                   /* gap not over yet */
        type_state = TYPE_SEND_DOWN;
        /* fall through */

    case TYPE_SEND_DOWN: {
        /* Skip anything the US table cannot type (non-ASCII, control codes). */
        unsigned char c;
        while ((c = (unsigned char)type_text[type_pos]) != '\0' &&
               (c >= 128 || conv_table[c][1] == 0))
            type_pos++;
        if (c == '\0') { typing_reset(); return; }    /* phrase finished */

        if (!tud_hid_ready())
            return;                                   /* previous report in flight */

        uint8_t keycode[6] = { 0 };
        keycode[0] = conv_table[c][1];
        uint8_t modifier = conv_table[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
        tud_hid_keyboard_report(REPORT_ID_NONE, modifier, keycode);
        type_state = TYPE_SEND_UP;
        return;
    }

    case TYPE_SEND_UP:
        if (!tud_hid_ready())
            return;                                   /* key-down not delivered yet */
        tud_hid_keyboard_report(REPORT_ID_NONE, 0, NULL);   /* all keys up */
        type_pos++;
        type_gap_until = millis() + CHAR_GAP_MS;
        type_state     = TYPE_GAP;
        return;
    }
}

/* --------------------------------------------------------------------- */
/* LED: on while typing, off otherwise                                    */
/* --------------------------------------------------------------------- */
static void led_task(void)
{
    board_led_set(typing_active());
}

/* --------------------------------------------------------------------- */
/* Button: debounce, count clicks, dispatch (image 2's logic, non-blocking)*/
/* --------------------------------------------------------------------- */
static uint32_t next_poll_ms = 0;
static bool     raw_last     = false;
static bool     stable       = false;   /* debounced level */
static uint32_t raw_since    = 0;
static unsigned clicks       = 0;
static uint32_t release_time = 0;

static void dispatch(unsigned n)
{
    if (!tud_mounted() || typing_active() || n < 1 || n > PHRASE_COUNT)
        return;                                       /* silently ignored */
    typing_start(PHRASES[n - 1]);
}

static void button_task(void)
{
    uint32_t now = millis();
    if ((int32_t)(now - next_poll_ms) < 0)
        return;
    next_poll_ms += POLL_INTERVAL_MS;
    if ((int32_t)(now - next_poll_ms) > 0)            /* fell behind: resync */
        next_poll_ms = now + POLL_INTERVAL_MS;

    bool raw = bootsel_pressed();

    /* Accept a new level only after DEBOUNCE_MS of stability. */
    if (raw != raw_last) {
        raw_last  = raw;
        raw_since = now;
    } else if (raw != stable && (now - raw_since) >= DEBOUNCE_MS) {
        stable = raw;
        if (stable) {
            /* accepted PRESS: counts only when nothing is being typed */
            if (!typing_active())
                clicks++;
        } else {
            /* accepted RELEASE */
            release_time = now;
        }
    }

    /* End of burst: released and quiet for CLICK_TIMEOUT_MS. */
    if (clicks && !stable && (now - release_time) >= CLICK_TIMEOUT_MS) {
        dispatch(clicks);
        clicks = 0;
    }
}

/* --------------------------------------------------------------------- */
/* main                                                                   */
/* --------------------------------------------------------------------- */
int main(void)
{
    board_led_init();                 /* starts with the LED off */
    tud_init(BOARD_TUD_RHPORT);

    raw_last = stable = bootsel_pressed();
    raw_since    = millis();
    next_poll_ms = millis() + POLL_INTERVAL_MS;

    while (true) {
        tud_task();        /* TinyUSB device task: MUST be called continuously */
        button_task();
        typing_task();
        led_task();
    }
}

/* --------------------------------------------------------------------- */
/* TinyUSB device callbacks (the others are weak no-ops in TinyUSB)       */
/* --------------------------------------------------------------------- */
void tud_mount_cb(void)
{
    typing_reset();                   /* fresh start on every (re)connect */
    clicks = 0;
}

void tud_umount_cb(void)
{
    typing_reset();
}

/* GET_REPORT control request: not needed for a keyboard. Returning 0 STALLs it. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

/* SET_REPORT: the host sends keyboard LED state (Caps Lock etc.). Ignored. */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}
