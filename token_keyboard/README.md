# token_keyboard: an RP2350 USB token as a one-button keyboard

Firmware experiments for a USB-A token built on the **RP2350** (Raspberry Pi
Pico 2 reference design; the "Feitian RP2350" board in the
[Bruce](https://github.com/cryptsoft/Bruce) firmware). The token has one
button, which is BOOTSEL, and one LED. Five small C images, built with
pico-sdk 2.x + TinyUSB, work up from "blink the LED" to "N clicks of the
button types phrase N as a USB keyboard". All five have been run on the
hardware.

## Hardware (confirmed on the token)

- RP2350A, revision A4, QFN60 package. Secure boot off, OTP untouched.
- **The button is BOOTSEL**, which is not a GPIO: it pulls the QSPI flash
  chip-select line (QSPI_SS) low. It is read at runtime by briefly floating that
  pad and sampling it; see `common/bootsel.c`.
- **LED on GP25, active-high** (two green LEDs behind a 510 ohm resistor).
- **Flash: 2 MiB.** `picotool info` reports the ROM default (`flash devinfo
  0x0c00`) because the size is not programmed into OTP; Bruce's firmware measured
  2 MiB on this token. `CMakeLists.txt` declares it, so the linker's FLASH region
  matches the chip.
- No custom board file: stock `PICO_BOARD=pico2`, platform `rp2350-arm-s`.
  Board constants live in `common/board_config.h`; a different board only needs
  the LED pin and polarity changed there (image 0's scan mode finds them).

## Layout

```
CMakeLists.txt            top level: board, 2 MiB flash, warnings, shared lib, images
pico_sdk_import.cmake     stock copy from pico-sdk/external
THIRD_PARTY_LICENSES.md   notices for the borrowed BOOTSEL routine and TinyUSB example code
common/
  board_config.h          BOARD_LED_PIN 25, active-high, board_led_init/set
  bootsel.h / bootsel.c   bool bootsel_pressed(void)  (runs from SRAM)
00_led_blink/             image 0: LED blink; -DLED_SCAN=ON builds a pin-scan variant
01_bootsel_status/        image 1: prints BOOTSEL pressed/released over USB serial
02_bootsel_clicks/        image 2: counts click bursts, prints "clicks: N" + timing
03_keyboard_alphabet/     image 3: USB HID keyboard typing a..z, one per second forever
04_click_phrases/         image 4: N clicks types phrase N (phrases.h) for N <= 5
```

Generated directories (`build/`, `build-scan/`, `out/`) are git-ignored.

## The images

| Image | UF2 | What it does | How to watch it |
|-------|-----|--------------|-----------------|
| 0 | `00_led_blink.uf2` | LED 500 ms on / 500 ms off | LED |
| 0 (scan) | `00_led_blink_scan.uf2` | Walks GP0-GP29 (except 23, 24, 29), one pin at a time, both polarities, printing which pin it is driving. For finding the LED on a different board. | LED + serial |
| 1 | `01_bootsel_status.uf2` | Polls BOOTSEL every 10 ms, prints `BOOTSEL: pressed` / `released` on change and a heartbeat each second; LED mirrors the button | serial |
| 2 | `02_bootsel_clicks.uf2` | Polls every 5 ms, 20 ms debounce, counts press+release clicks, prints `clicks: N` and the press/gap timings 400 ms after the last release; LED flashes per click | serial |
| 3 | `03_keyboard_alphabet.uf2` | HID keyboard, types `a`..`z` forever, one per second, starting 2 s after the host connects; LED blinks 1 s when mounted | text editor |
| 4 | `04_click_phrases.uf2` | HID keyboard, N clicks types phrase N; LED on only while typing | text editor |

Images 1 and 2 and the scan image enumerate as a USB serial port (115200 baud,
`/dev/ttyACM0` on Linux). Images 3 and 4 enumerate as a keyboard only (no
serial; a CDC interface would clash with the HID descriptors).

### Image 4: `04_click_phrases`

- Click the button N times. 400 ms of silence after the last release ends the
  burst, and phrase N from `04_click_phrases/phrases.h` is typed into whatever
  window has keyboard focus. **Click into a text editor first.**
- Counts of 1..5 type; any other count, or clicks while not connected, do
  nothing.
- LED: on from the first character to the last, off at all other times. No idle
  blink.
- Presses while typing are ignored; a phrase always runs to the end. Unplugging
  the token is the only way to cut it short.
- No Enter after a phrase. Trailing spaces in `phrases.h` are typed.
- US keyboard layout is assumed (punctuation will differ on other layouts). The
  host's Caps Lock state inverts letter case because the device cannot see it.
- Speed: one key-down report and one all-up report per character, each waiting
  for a host poll (`HID_POLL_INTERVAL_MS` = 10 in `usb_descriptors.c`), plus
  `CHAR_GAP_MS` = 5. About 25 ms per character, so the 147-character phrase
  takes about 4 s. Lower `HID_POLL_INTERVAL_MS` to 1 for roughly 7 ms per
  character.
- To change the text: edit `phrases.h`, rebuild, reflash. Plain ASCII only.
  **Never put passwords or other secrets in it.**

## Toolchain

Tested with pico-sdk 2.3.1 (TinyUSB 0.18.0), `arm-none-eabi-gcc` 13.2.1,
CMake 3.28, Ninja 1.11 and picotool 2.3.1 on Ubuntu 24.04. The official
[Getting started with Raspberry Pi Pico-series](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)
document covers installation on other systems.

- [pico-sdk](https://github.com/raspberrypi/pico-sdk), cloned with
  `git submodule update --init` so TinyUSB is present. Point `PICO_SDK_PATH`
  at it (or pass `-DPICO_SDK_PATH=...` to CMake).
- Ubuntu packages: `cmake ninja-build gcc-arm-none-eabi libnewlib-arm-none-eabi`.
  If a different `arm-none-eabi-gcc` is first on `PATH`, pin the one you want
  with `-DPICO_TOOLCHAIN_PATH=<prefix>` (for the Ubuntu package, `/usr`).
- [picotool](https://github.com/raspberrypi/picotool) is optional. If none is
  installed the SDK builds a USB-less one itself for generating the `.uf2`
  files. Installed with USB support (needs `libusb-1.0-0-dev pkg-config`) it
  also flashes and inspects the token.

### Building

```bash
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=<path to pico-sdk>
cmake --build build          # all five images -> out/*.uf2
```

The pin-scan variant of image 0 uses its own build directory:

```bash
cmake -S . -B build-scan -G Ninja -DPICO_SDK_PATH=<path to pico-sdk> -DLED_SCAN=ON
cmake --build build-scan --target 00_led_blink    # -> out/00_led_blink_scan.uf2
```

All targets compile with `-Wall -Wextra` and no warnings. Every `.uf2` is copied
into `out/` after its link step.

### Flashing and serial

1. Unplug the token. Hold the button, plug it in, release. A USB drive named
   `RP2350` appears.
2. Either copy a `.uf2` from `out/` onto that drive, or run
   `picotool load -x out/<image>.uf2` (`-x` reboots into the new firmware).
   `picotool info -a` while in BOOTSEL mode shows the chip and the program
   currently in flash. As a non-root Linux user, picotool needs the udev rule
   shipped in its repository (`udev/60-picotool.rules`).
3. For images 1, 2 and the scan image, open the serial port, for example
   `picocom -b 115200 /dev/ttyACM0` (quit with `Ctrl-A` then `Ctrl-X`). A
   non-root user usually needs to be in the `dialout` group.
4. For images 3 and 4, click into a text editor before the token starts typing.

## Design notes

- `common/bootsel.c` is the pico-examples `get_bootsel_button()` routine as
  carried in Bruce's RP2350-validated C firmware (BSD-3-Clause, attribution kept
  in the file). It runs from SRAM (`__no_inline_not_in_flash_func`), masks
  interrupts only for the sample, floats the QSPI_SS pad via the IO_QSPI output
  override, reads `sio_hw->gpio_hi_in` with the SDK's
  `SIO_GPIO_HI_IN_QSPI_CSN_BITS` (bit 27 on the RP2350; bit 1 on the RP2040,
  which is what breaks naive RP2040 ports), and requires 8 consecutive low
  samples before reporting a press. Each call blacks out flash for roughly
  10-20 microseconds. All images are single-core.
- Images 2 and 4 share the same click logic: 5 ms poll, 20 ms debounce, 400 ms
  inter-click timeout. Measured on this hardware: presses 115-195 ms, gaps
  between clicks 195-245 ms, so both constants have wide margins.
- Images 3 and 4 are one HID keyboard interface (boot protocol, no report IDs),
  TinyUSB's example VID/PID `0xCafe:0x4004`, started from pico-examples
  `usb/device/dev_hid_composite` and trimmed to the keyboard. Typing is a
  timestamp state machine; `tud_task()` runs every loop iteration.
- Image 3 waits 2 s after the host configures the device before its first
  letter: a key sent immediately after enumeration was dropped by the desktop,
  which had not yet opened the new input device.

## Cautions

- The pin scan drives GPIOs as outputs. It skips GP23, GP24 and GP29 (power
  mode, VBUS sense, VSYS sense on the reference design) and drives one pin at a
  time; check the list before running it on a board with other wiring.
- Images 3 and 4 type into whatever window has focus. Keep secrets out of
  `phrases.h`.
- Nothing here touches OTP, secure boot or fuses. `picotool otp` and `seal`
  operations are irreversible; this project never needs them.

## Licensing

`THIRD_PARTY_LICENSES.md` lists the borrowed code (Raspberry Pi's BOOTSEL
routine and `pico_sdk_import.cmake`, BSD-3-Clause; TinyUSB example-derived
keyboard code, MIT) with the full license texts.
