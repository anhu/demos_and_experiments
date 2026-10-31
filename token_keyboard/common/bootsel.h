/* bootsel.h -- read the BOOTSEL button at runtime on the RP2350.
 *
 * The button is not on a GPIO: it pulls the flash chip-select line (QSPI_SS)
 * low. bootsel_pressed() briefly floats that line, samples it, and hands it
 * back to the flash. Each call blacks out flash for ~10-20 microseconds with
 * interrupts off, so:
 *   - do not call it faster than every few milliseconds,
 *   - never start core 1 in a program that uses it (keep everything single-core),
 *   - never call it while a flash write is in progress.
 * See bootsel.c for the mechanism and provenance.
 */
#ifndef BOOTSEL_H
#define BOOTSEL_H

#include <stdbool.h>

/* true while the BOOTSEL button is held down. */
bool bootsel_pressed(void);

#endif /* BOOTSEL_H */
