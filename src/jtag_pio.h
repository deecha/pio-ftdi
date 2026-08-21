#ifndef JTAG_PIO_H
#define JTAG_PIO_H

#include <stdint.h>
#include <stdbool.h>

void     jtag_pio_init(void);

// Full state-machine reset: clears FIFOs and returns the SM to the top of the
// program. mpsse_reset() alone only clears parser state, so a stale word left
// in a PIO FIFO survived every software reset and offset all subsequent reads
// until the board was physically replugged.
void     jtag_pio_reset(void);

// Shift up to 32 bits. `data` is LSB-first on the wire.
// Returns TDO right-aligned, LSB = first bit received.
uint32_t jtag_shift_data(unsigned nbits, uint32_t data);

// TMS shift (MPSSE 0x4x/0x6x). TDI is held at `tdi_level` for the duration.
uint32_t jtag_shift_tms(unsigned nbits, uint32_t tms, bool tdi_level);

// Clock TCK with no data change (MPSSE 0x8E/0x8F).
void     jtag_clock_only(uint32_t nbits);

// Apply an MPSSE clock divisor. base = 60 MHz, or 12 MHz when div-by-5 is on.
void     jtag_set_divisor(uint16_t divisor, bool div_by_5);

bool     jtag_tdo_level(void);
void     jtag_set_static(bool tck, bool tdi, bool tms);
void     jtag_get_static(bool *tck, bool *tdi, bool *tms);

#endif