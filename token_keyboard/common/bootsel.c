/* bootsel.c -- runtime BOOTSEL-button sampling via the QSPI chip-select line.
 *
 * Derived from Raspberry Pi pico-examples, picoboard/button/button.c
 * (`get_bootsel_button`), Copyright (c) 2020 Raspberry Pi (Trading) Ltd.,
 * SPDX-License-Identifier: BSD-3-Clause, by way of the Bruce firmware's
 * c/firmware/src/bootsel.c, which validated this exact routine on the
 * Feitian RP2350 token (and added the multi-sample vote).
 *
 * How it works
 * ------------
 * On Pico 2 reference-design boards the BOOTSEL button shorts the flash
 * chip-select (QSPI_SS) to ground through a 1 kohm resistor. Only the boot
 * ROM looks at it, at reset. To read it at runtime we:
 *   1. override the QSPI_SS pad's output-enable so the chip-select driver
 *      goes high-impedance (OEOVER = disable),
 *   2. wait a few microseconds for the line to settle: the pad pull-up
 *      takes it high, or the pressed button takes it low,
 *   3. read the line through SIO's "HI" GPIO input bank, which mirrors the
 *      six QSPI pads (QSPI_CSN is bit 27 on the RP2350; it was bit 1 on the
 *      RP2040, which is why naive RP2040 ports misread on this chip. The
 *      SDK constant SIO_GPIO_HI_IN_QSPI_CSN_BITS is correct for both),
 *   4. put the override back to normal so the flash is reachable again.
 *
 * Why this must run from RAM with interrupts off
 * ----------------------------------------------
 * While chip-select is floating the flash cannot be read, so any instruction
 * fetch or data load from the flash window would fault. Therefore:
 *   - __no_inline_not_in_flash_func places this function in SRAM,
 *   - interrupts are masked for the whole window (handlers live in flash),
 *   - the settle delay is a volatile busy loop (sleep_us() lives in flash),
 *   - the SDK helpers used here are all force-inlined, so no call leaves RAM.
 * Core 1 must not be executing from flash either; these images never start it.
 */

#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

#include "bootsel.h"

bool __no_inline_not_in_flash_func(bootsel_pressed)(void)
{
    /* QSPI_SS is chip-select, index 1 in the IO_QSPI GPIO array. */
    const uint CS_PIN_INDEX = 1;

    uint32_t flags = save_and_disable_interrupts();

    /* Float the chip-select pad (OEOVER = disable the output driver). */
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    /* Settle. No flash-resident calls (e.g. sleep_us) allowed in here. */
    for (volatile int i = 0; i < 1000; ++i)
        ;

    /* The button pulls the line LOW when pressed. Sample several times and
     * require unanimity: a real press holds the line low for milliseconds,
     * while a coupled transient on the QSPI lines does not. */
    bool pressed = true;
    for (int s = 0; s < 8; ++s) {
        if (sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS) {
            pressed = false;          /* read HIGH => not pressed */
            break;
        }
        for (volatile int i = 0; i < 200; ++i)
            ;
    }

    /* Hand chip-select back to the flash controller BEFORE touching flash. */
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return pressed;
}
