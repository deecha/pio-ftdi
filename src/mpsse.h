#ifndef MPSSE_H
#define MPSSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void mpsse_reset(void);

// Continue any in-flight read-only shift. Call from the main loop: these
// commands carry no payload on the wire, so mpsse_feed() alone never advances
// them once the response ring fills.
void mpsse_pump(void);

// Bring-up: snapshot of host GPIO activity. See mpsse.c.
void mpsse_gpio_debug(uint8_t out[8]);

// Opcode trace (bring-up only)
void     mpsse_trace_clear(void);
uint16_t mpsse_trace_len(void);
uint16_t mpsse_trace_read(uint16_t off, uint8_t *dst, uint16_t max);

// Feed host->device bytes. Returns bytes consumed; may be short if the
// response ring filled up, in which case call again after draining.
size_t mpsse_feed(const uint8_t *buf, size_t len);

// Response ring (device->host payload, no status bytes -- ftdi_usb adds those)
size_t mpsse_resp_count(void);
size_t mpsse_resp_read(uint8_t *dst, size_t max);
bool   mpsse_send_immediate(void);   // consumes the 0x87 flag

#endif