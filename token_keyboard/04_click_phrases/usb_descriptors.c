/* usb_descriptors.c -- USB descriptors for image 4: one HID keyboard (same as image 3).
 *
 * Trimmed from pico-examples usb/device/dev_hid_composite/usb_descriptors.c
 * (The MIT License, Copyright (c) 2019 Ha Thach, tinyusb.org): the mouse,
 * consumer-control and gamepad reports are gone, and the keyboard report has
 * no report ID.
 *
 * "Descriptors" are the tables a USB device hands to the host during
 * enumeration to say what it is. HID = Human Interface Device, the USB
 * class for keyboards and mice.
 */

#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_descriptors.h"

/* TinyUSB's test vendor ID, as used by the pico-examples. PID 0x4004 follows
 * the example's scheme (bit 2 set = HID present). */
#define USB_VID   0xCafe
#define USB_PID   0x4004
#define USB_BCD   0x0200

/* --------------------------------------------------------------------- */
/* Device descriptor                                                      */
/* --------------------------------------------------------------------- */
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

/* --------------------------------------------------------------------- */
/* HID report descriptor: a standard boot-protocol keyboard, no report ID  */
/* --------------------------------------------------------------------- */
static uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

/* --------------------------------------------------------------------- */
/* Configuration descriptor: one interface, one IN endpoint                */
/* --------------------------------------------------------------------- */
enum {
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID         0x81   /* endpoint 1, IN (device -> host) */
/* Host polls the keyboard endpoint this often. Each report (key-down, key-up)
 * waits for one poll, so this bounds the typing speed: ~2x this per character. */
#define HID_POLL_INTERVAL_MS 10

static uint8_t const desc_configuration[] = {
    /* Config number, interface count, string index, total length, attributes, power in mA */
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),

    /* Interface number, string index, protocol, report descriptor len,
     * EP In address, size, polling interval (ms). KEYBOARD protocol means it
     * also works as a "boot" keyboard (BIOS, early boot). */
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, HID_POLL_INTERVAL_MS)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/* --------------------------------------------------------------------- */
/* String descriptors                                                     */
/* --------------------------------------------------------------------- */
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};

static char const *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },     /* 0: supported language: English (0x0409) */
    "bootsel-experiments",            /* 1: Manufacturer */
    "RP2350 click phrases",           /* 2: Product */
    NULL,                             /* 3: Serial: filled from the chip's unique ID */
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    size_t chr_count;

    switch (index) {
    case STRID_LANGID:
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
        break;

    case STRID_SERIAL: {
        /* 16 hex digits from the flash chip's unique ID, plus NUL. */
        char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        pico_get_unique_board_id_string(serial, sizeof serial);
        chr_count = strlen(serial);
        for (size_t i = 0; i < chr_count; i++)
            _desc_str[1 + i] = (uint16_t)serial[i];
        break;
    }

    default:
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
            return NULL;

        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
        if (chr_count > max_count)
            chr_count = max_count;

        for (size_t i = 0; i < chr_count; i++)
            _desc_str[1 + i] = (uint16_t)str[i];   /* ASCII -> UTF-16 */
        break;
    }

    /* First byte is the length (including this header), second is the type. */
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
