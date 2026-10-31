/* Image 3: 03_keyboard_alphabet
 *
 * Enumerates as a USB HID keyboard and types a, b, c ... z, then wraps to a,
 * forever, with one second between letters. For each letter a key-down
 * report is sent, then (once that has gone out) an all-keys-up report.
 *
 * The pause is NON-blocking: the main loop keeps calling tud_task() so USB is
 * always serviced, and a timestamp decides when the next letter is due.
 *
 * The first letter is sent FIRST_LETTER_DELAY_MS after the host configures the
 * device, not immediately. Tested 2026-10-30: typing straight after mount lost
 * the 'a' (the desktop had not yet opened the new keyboard device, so the first
 * key event had no listener), and the editor received 'b' first. Every
 * (re)connect restarts the alphabet from 'a'.
 *
 * No stdio over USB in this image (a CDC interface would clash with the HID
 * descriptors). The LED shows liveness: fast blink until the host has
 * configured the device, 1 s blink once mounted, slow blink when suspended.
 *
 * HID sends key POSITIONS, not characters: HID_KEY_A ... HID_KEY_Z are
 * 0x04 ... 0x1D. The host's keyboard layout decides what appears; US layout
 * is assumed.
 *
 * Started from pico-examples usb/device/dev_hid_composite/main.c
 * (The MIT License, Copyright (c) 2019 Ha Thach, tinyusb.org), trimmed to the
 * keyboard interface and rewritten around a one-letter-per-second schedule.
 */

#include "pico/stdlib.h"
#include "tusb.h"
#include "board_config.h"
#include "usb_descriptors.h"

#define LETTER_INTERVAL_MS    1000   /* pause between letters                      */
#define FIRST_LETTER_DELAY_MS 2000   /* grace period after mount before the first 'a' */

/* LED blink period (ms) per USB state. */
enum {
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED     = 1000,
    BLINK_SUSPENDED   = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

static inline uint32_t millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/* TinyUSB 0.18 declares this "API implemented by user"; the RP2 port does
 * not call it, but providing it costs nothing and future-proofs the build. */
uint32_t tusb_time_millis_api(void)
{
    return millis();
}

/* --------------------------------------------------------------------- */
/* Typing task                                                            */
/* --------------------------------------------------------------------- */
enum kb_state { WAIT_INTERVAL, SEND_DOWN, SEND_UP };

/* File scope so tud_mount_cb() can restart the sequence. The callbacks run
 * inside tud_task(), i.e. in the same main-loop context as keyboard_task(),
 * so no locking is needed. */
static enum kb_state state        = WAIT_INTERVAL;
static uint32_t      next_due_ms  = 0;
static uint8_t       letter_index = 0;   /* 0 = 'a' ... 25 = 'z' */

static void keyboard_task(void)
{
    switch (state) {
    case WAIT_INTERVAL:
        if (!tud_mounted())
            return;                                   /* host not ready yet */
        if ((int32_t)(millis() - next_due_ms) < 0)
            return;                                   /* not time yet */
        state = SEND_DOWN;
        /* fall through */

    case SEND_DOWN:
        if (!tud_hid_ready())
            return;                                   /* previous report still in flight */
        {
            uint8_t keycode[6] = { 0 };
            keycode[0] = (uint8_t)(HID_KEY_A + letter_index);
            tud_hid_keyboard_report(REPORT_ID_NONE, 0 /* no modifiers */, keycode);
        }
        state = SEND_UP;
        return;

    case SEND_UP:
        if (!tud_hid_ready())
            return;                                   /* key-down not yet delivered */
        tud_hid_keyboard_report(REPORT_ID_NONE, 0, NULL);   /* all keys up */
        letter_index = (uint8_t)((letter_index + 1) % 26);
        next_due_ms  = millis() + LETTER_INTERVAL_MS;
        state = WAIT_INTERVAL;
        return;
    }
}

/* --------------------------------------------------------------------- */
/* LED liveness task                                                      */
/* --------------------------------------------------------------------- */
static void led_task(void)
{
    static uint32_t start_ms  = 0;
    static bool     led_state = false;

    if (millis() - start_ms < blink_interval_ms)
        return;
    start_ms += blink_interval_ms;

    led_state = !led_state;
    board_led_set(led_state);
}

/* --------------------------------------------------------------------- */
/* main                                                                   */
/* --------------------------------------------------------------------- */
int main(void)
{
    board_led_init();

    /* Bring up the USB device stack on root-hub port 0. */
    tud_init(BOARD_TUD_RHPORT);

    while (true) {
        tud_task();        /* TinyUSB device task: MUST be called continuously */
        led_task();
        keyboard_task();
    }
}

/* --------------------------------------------------------------------- */
/* TinyUSB device callbacks                                               */
/* --------------------------------------------------------------------- */
void tud_mount_cb(void)
{
    blink_interval_ms = BLINK_MOUNTED;
    /* Host has configured us. Start over from 'a', after a grace period so the
     * host's input stack has time to start listening to the new keyboard. */
    state        = WAIT_INTERVAL;
    letter_index = 0;
    next_due_ms  = millis() + FIRST_LETTER_DELAY_MS;
}

void tud_umount_cb(void)
{
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

void tud_resume_cb(void)
{
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
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
