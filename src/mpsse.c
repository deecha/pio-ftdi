// mpsse.c - MPSSE command interpreter
//
// Incremental byte-at-a-time state machine, so commands may straddle USB
// packet boundaries. Everything is executed synchronously against the PIO,
// which is fine: full-speed USB (~800 KB/s) is the bottleneck, not JTAG.

#include "mpsse.h"
#include "jtag_pio.h"
#include "board_config.h"
#include "hardware/gpio.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Response ring
// ---------------------------------------------------------------------------
#define RESP_SZ 4096
static uint8_t  resp[RESP_SZ];
static volatile uint16_t r_head, r_tail;

static inline size_t resp_free(void) {
    return (RESP_SZ - 1) - ((r_head - r_tail) & (RESP_SZ - 1));
}
size_t mpsse_resp_count(void) { return (r_head - r_tail) & (RESP_SZ - 1); }

static inline void resp_put(uint8_t b) {
    uint16_t n = (r_head + 1) & (RESP_SZ - 1);
    if (n == r_tail) return;              // drop; caller guarantees headroom
    resp[r_head] = b;
    r_head = n;
}

size_t mpsse_resp_read(uint8_t *dst, size_t max) {
    size_t n = 0;
    while (n < max && r_tail != r_head) {
        dst[n++] = resp[r_tail];
        r_tail = (r_tail + 1) & (RESP_SZ - 1);
    }
    return n;
}

// Opcode trace: every byte consumed in S_CMD. Lets a first (working) session
// be diffed against a second (failing) one to see where they diverge.
#define TRACE_SZ 1024
static uint8_t  trace[TRACE_SZ];
static uint16_t trace_n;

void mpsse_trace_clear(void) { trace_n = 0; }
uint16_t mpsse_trace_len(void) { return trace_n; }
uint16_t mpsse_trace_read(uint16_t off, uint8_t *dst, uint16_t max)
{
    if (off >= trace_n) return 0;
    uint16_t n = trace_n - off;
    if (n > max) n = max;
    for (uint16_t i = 0; i < n; i++) dst[i] = trace[off + i];
    return n;
}

static bool flush_flag;
bool mpsse_send_immediate(void) { bool f = flush_flag; flush_flag = false; return f; }

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
typedef enum {
    S_CMD = 0,
    S_LEN_LO, S_LEN_HI, S_WBYTES,
    S_BITLEN, S_WBITS,
    S_TMSLEN, S_TMSDATA,
    S_GPIO_VAL, S_GPIO_DIR,
    S_DIV_LO, S_DIV_HI,
    S_CLKN_LO, S_CLKN_HI, S_CLKN_BITS,
    S_RDONLY,          // read-only shift in progress, consumes no input bytes
    S_WAIT_SKIP,
} state_t;

static state_t  st;
static uint8_t  cmd;
static uint32_t rem;            // bytes still expected / to shift
static uint8_t  nbits;
static uint16_t divisor;
static bool     div5;
static bool     loopback;
static uint8_t  adbus_val, adbus_dir, acbus_val, acbus_dir;

// Bring-up instrumentation: which GPIO bits the host actually drives, and low.
// Efinix hosts assert CRESET_N through one of these to force the FPGA out of
// user mode before programming -- this is how we find out which bit.
static uint8_t adbus_low_seen, acbus_low_seen, n_cmd80, n_cmd82;

void mpsse_gpio_debug(uint8_t out[8])
{
    out[0] = adbus_val;      out[1] = adbus_dir;
    out[2] = acbus_val;      out[3] = acbus_dir;
    out[4] = adbus_low_seen; out[5] = acbus_low_seen;
    out[6] = n_cmd80;        out[7] = n_cmd82;
}

static const int adbus_aux[4] = ADBUS_AUX_PINS;
static const int acbus_aux[8] = ACBUS_AUX_PINS;

// command bit fields
#define C_NEGW(c)  ((c) & 0x01)
#define C_BITS(c)  ((c) & 0x02)
#define C_NEGR(c)  ((c) & 0x04)
#define C_LSB(c)   ((c) & 0x08)
#define C_WR(c)    ((c) & 0x10)
#define C_RD(c)    ((c) & 0x20)
#define C_TMS(c)   ((c) & 0x40)

static inline uint8_t bitrev8(uint8_t v) {
    v = (uint8_t)((v >> 4) | (v << 4));
    v = (uint8_t)(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
    v = (uint8_t)(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
    return v;
}

void mpsse_reset(void)
{
    st = S_CMD; rem = 0; nbits = 0;
    divisor = 0; div5 = true;            // FT2232H powers up with div-by-5 on
    loopback = false; flush_flag = false;
    r_head = r_tail = 0;
    adbus_val = adbus_dir = acbus_val = acbus_dir = 0;
    jtag_pio_reset();     // FIFOs too, not just parser state
    // adbus_low_seen / acbus_low_seen / counters deliberately persist
    jtag_set_divisor(divisor, div5);
}

// ---------------------------------------------------------------------------
static void pump_readonly(void);

static void bad_command(uint8_t c)
{
    resp_put(0xFA);      // hosts use this to resync
    resp_put(c);
}

static void shift_byte(uint8_t out)
{
    uint8_t o = C_LSB(cmd) ? out : bitrev8(out);
    uint32_t in = jtag_shift_data(8, loopback ? 0 : o);
    if (loopback) in = o;
    if (C_RD(cmd)) resp_put(C_LSB(cmd) ? (uint8_t)in : bitrev8((uint8_t)in));
}

static void shift_bits(uint8_t out)
{
    uint8_t n = nbits;
    uint8_t o = C_LSB(cmd) ? out : bitrev8(out) >> (8 - n);
    uint32_t in = jtag_shift_data(n, loopback ? 0 : o);
    if (loopback) in = o;
    if (C_RD(cmd)) {
        // Bit-mode reads come back left-aligned on a real FT2232H: the host
        // right-shifts by (8 - n). Match that or OpenOCD misreads short scans.
        uint8_t v = C_LSB(cmd) ? (uint8_t)(in << (8 - n))
                               : bitrev8((uint8_t)in);
        resp_put(v);
    }
}

static void apply_gpio_low(void)
{
    n_cmd80++;
    adbus_low_seen |= (uint8_t)(adbus_dir & ~adbus_val);   // driven low
    // MPSSE ADBUS mapping: 0=TCK 1=TDI 2=TDO 3=TMS 4..7=aux
    bool tck = (adbus_dir & 0x01) && (adbus_val & 0x01);
    bool tdi = (adbus_dir & 0x02) && (adbus_val & 0x02);
    bool tms = (adbus_dir & 0x08) && (adbus_val & 0x08);
    jtag_set_static(tck, tdi, tms);

    for (int i = 0; i < 4; i++) {
        int g = adbus_aux[i];
        if (g < 0) continue;
        bool out = (adbus_dir >> (4 + i)) & 1;
        gpio_init(g);
        gpio_set_dir(g, out);
        if (out) gpio_put(g, (adbus_val >> (4 + i)) & 1);
    }
}

static void apply_gpio_high(void)
{
    n_cmd82++;
    acbus_low_seen |= (uint8_t)(acbus_dir & ~acbus_val);
    for (int i = 0; i < 8; i++) {
        int g = acbus_aux[i];
        if (g < 0) continue;
        bool out = (acbus_dir >> i) & 1;
        gpio_init(g);
        gpio_set_dir(g, out);
        if (out) gpio_put(g, (acbus_val >> i) & 1);
    }
}

static uint8_t read_gpio_low(void)
{
    bool tck, tdi, tms;
    jtag_get_static(&tck, &tdi, &tms);
    uint8_t v = (tck ? 0x01 : 0) | (tdi ? 0x02 : 0) | (tms ? 0x08 : 0);
    if (jtag_tdo_level()) v |= 0x04;
    for (int i = 0; i < 4; i++) {
        int g = adbus_aux[i];
        if (g >= 0 && gpio_get(g)) v |= (uint8_t)(1u << (4 + i));
    }
    return v;
}

// Read-only byte shifts (0x28/0x2C etc.) consume no bytes from the wire, so
// they cannot be paced by the input loop. Running the whole length in one go
// overflowed the response ring on long reads and silently dropped bytes; the
// host then waited forever for data that was never sent. Shift only as far as
// there is room, and resume from mpsse_pump().
static void pump_readonly(void)
{
    while (rem && resp_free() >= 8) {
        shift_byte(0x00);
        rem--;
    }
    if (rem == 0) st = S_CMD;
}

void mpsse_pump(void)
{
    if (st == S_RDONLY) pump_readonly();
}

// Length known; decide whether data bytes follow on the wire.
static void length_ready(void)
{
    if (C_WR(cmd)) {
        st = C_BITS(cmd) ? S_WBITS : S_WBYTES;
        return;
    }
    // read-only: no payload follows, shift dummies as ring space allows
    if (C_BITS(cmd)) {
        shift_bits(0x00);
        st = S_CMD;
    } else {
        st = S_RDONLY;
        pump_readonly();
    }
}

// ---------------------------------------------------------------------------
size_t mpsse_feed(const uint8_t *buf, size_t len)
{
    size_t i = 0;

    // A read-only run still in flight blocks new commands until it drains.
    if (st == S_RDONLY) {
        pump_readonly();
        if (st == S_RDONLY) return 0;
    }

    for (; i < len; i++) {
        // Worst case a single input byte emits one response byte (plus 0xFA
        // pairs). Bail out early so the caller can drain and re-feed.
        if (resp_free() < 4) break;

        uint8_t b = buf[i];

        switch (st) {
        case S_CMD:
            if (trace_n < TRACE_SZ) trace[trace_n++] = b;
            cmd = b;
            if (b < 0x80) {
                // Bit 6 (TMS) is itself a write flag: TMS commands drive the
                // TMS pin, not TDI, so bit 4 is legitimately clear on them.
                // Requiring bit 4 or bit 5 here rejected 0x4B -- the command
                // every host uses for TAP state transitions.
                if (!C_TMS(cmd) && !C_WR(cmd) && !C_RD(cmd)) { bad_command(b); break; }
                if (C_TMS(cmd)) { st = S_TMSLEN; }
                else if (C_BITS(cmd)) { st = S_BITLEN; }
                else { st = S_LEN_LO; }
                break;
            }
            switch (b) {
            case 0x80: st = S_GPIO_VAL; break;
            case 0x82: st = S_GPIO_VAL; break;
            case 0x81: resp_put(read_gpio_low()); break;
            case 0x83: resp_put(acbus_val);       break;
            case 0x84: loopback = true;  break;
            case 0x85: loopback = false; break;
            case 0x86: st = S_DIV_LO; break;
            case 0x87: flush_flag = true; break;
            case 0x88: case 0x89: st = S_CMD; break;      // wait on GPIOL1: no-op
            case 0x8A: div5 = false; jtag_set_divisor(divisor, div5); break;
            case 0x8B: div5 = true;  jtag_set_divisor(divisor, div5); break;
            case 0x8C: case 0x8D: break;                  // 3-phase: accept, ignore
            case 0x8E: st = S_CLKN_BITS; break;
            case 0x8F: st = S_CLKN_LO;   break;
            case 0x94: case 0x95: case 0x9C: case 0x9D: break;
            case 0x96: case 0x97: break;                  // adaptive clocking
            default:   bad_command(b); break;
            }
            break;

        case S_LEN_LO:  rem  = b;                 st = S_LEN_HI; break;
        case S_LEN_HI:  rem |= ((uint32_t)b << 8); rem += 1; length_ready(); break;

        case S_WBYTES:
            shift_byte(b);
            if (--rem == 0) st = S_CMD;
            break;

        case S_BITLEN:
            nbits = (uint8_t)((b & 0x07) + 1);
            rem = 1;
            length_ready();
            break;

        case S_WBITS:
            shift_bits(b);
            st = S_CMD;
            break;

        case S_TMSLEN:
            nbits = (uint8_t)((b & 0x07) + 1);
            st = S_TMSDATA;
            break;

        case S_TMSDATA: {
            bool tdi = (b & 0x80) != 0;
            uint32_t in = jtag_shift_tms(nbits, b & 0x7F, tdi);
            if (C_RD(cmd)) resp_put((uint8_t)(in << (8 - nbits)));
            st = S_CMD;
            break;
        }

        case S_GPIO_VAL:
            if (cmd == 0x80) adbus_val = b; else acbus_val = b;
            st = S_GPIO_DIR;
            break;

        case S_GPIO_DIR:
            if (cmd == 0x80) { adbus_dir = b; apply_gpio_low(); }
            else             { acbus_dir = b; apply_gpio_high(); }
            st = S_CMD;
            break;

        case S_DIV_LO: divisor = b; st = S_DIV_HI; break;
        case S_DIV_HI:
            divisor |= (uint16_t)b << 8;
            jtag_set_divisor(divisor, div5);
            st = S_CMD;
            break;

        case S_RDONLY:
            return i;   // unreachable; handled above

        case S_CLKN_BITS:
            jtag_clock_only((b & 0x07) + 1u);
            st = S_CMD;
            break;

        case S_CLKN_LO: rem = b; st = S_CLKN_HI; break;
        case S_CLKN_HI:
            rem |= ((uint32_t)b << 8);
            jtag_clock_only((rem + 1u) * 8u);
            st = S_CMD;
            break;

        default:
            st = S_CMD;
            break;
        }
    }
    return i;
}