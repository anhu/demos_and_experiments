/* tusb_config.h -- TinyUSB configuration for image 4 (HID keyboard only, same as image 3).
 * Trimmed from pico-examples usb/device/dev_hid_composite (MIT, Ha Thach).
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* RHPort = "root hub port": the RP2350 has one USB controller, port 0. */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

/* CFG_TUSB_MCU is supplied by the pico-sdk's tinyusb_device target. */
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

/* Device stack on, full speed (12 Mbit/s, all the RP2350 PHY does). */
#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__ ((aligned(4)))
#endif

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

/* ---- USB classes: ONE HID interface, nothing else ---- */
#define CFG_TUD_HID               1
#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

/* HID endpoint buffer: must hold a keyboard report (8 bytes). */
#define CFG_TUD_HID_EP_BUFSIZE    16

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
