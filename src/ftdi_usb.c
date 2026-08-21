// ftdi_usb.c - FT2232 personality on RP2350
//
// Two pieces the rest of the world gets wrong and then loses a weekend to:
//
//  1. EVERY bulk IN packet carries a 2-byte modem/line status prefix.
//  2. When there is no data, the device must still emit a bare 2-byte packet
//     on the latency-timer cadence, or libftdi's read blocks forever.
//
// Both are handled in ftdi_usb_task().

#include "tusb.h"
#include "device/usbd_pvt.h"
#include "pico/unique_id.h"
#include "pico/time.h"

#include "board_config.h"
#include "mpsse.h"
#include "jtag_pio.h"

#define EPA_OUT 0x02
#define EPA_IN  0x81
#define EPB_OUT 0x04
#define EPB_IN  0x83
#define EP_SIZE 64

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = FTDI_VID,
    .idProduct          = FTDI_PID,
    .bcdDevice          = FTDI_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&desc_device; }

#if FTDI_SINGLE_CHANNEL
#define ITF_COUNT     1
#define CFG_TOTAL_LEN (9 + 1 * (9 + 7 + 7))
#else
#define ITF_COUNT     2
#define CFG_TOTAL_LEN (9 + 2 * (9 + 7 + 7))
#endif

static const uint8_t desc_configuration[] = {
    // config
    9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(CFG_TOTAL_LEN), ITF_COUNT, 1, 0,
    0x80, 250 / 2,                        // bus powered, 250 mA

    // interface A (JTAG / MPSSE)
    9, TUSB_DESC_INTERFACE, 0, 0, 2, 0xFF, 0xFF, 0xFF, 0,
    7, TUSB_DESC_ENDPOINT, EPA_IN,  TUSB_XFER_BULK, U16_TO_U8S_LE(EP_SIZE), 0,
    7, TUSB_DESC_ENDPOINT, EPA_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(EP_SIZE), 0,

#if !FTDI_SINGLE_CHANNEL
    // interface B: answered, but only as an idle UART
    9, TUSB_DESC_INTERFACE, 1, 0, 2, 0xFF, 0xFF, 0xFF, 0,
    7, TUSB_DESC_ENDPOINT, EPB_IN,  TUSB_XFER_BULK, U16_TO_U8S_LE(EP_SIZE), 0,
    7, TUSB_DESC_ENDPOINT, EPB_OUT, TUSB_XFER_BULK, U16_TO_U8S_LE(EP_SIZE), 0,
#endif
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

static uint16_t _desc_str[32];
static char serial_str[12];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    const char *s;
    uint8_t chr_count;

    if (index == 0) {
        _desc_str[1] = 0x0409;
        chr_count = 1;
    } else {
        switch (index) {
        case 1: s = FTDI_STR_MANUFACTURER; break;
        case 2: s = FTDI_STR_PRODUCT;      break;
        case 3: {
            pico_unique_board_id_t id;
            pico_get_unique_board_id(&id);
            // FTDI serials are 8 chars starting with the type letter pair.
            static const char hexd[] = "0123456789ABCDEF";
            serial_str[0] = 'F'; serial_str[1] = 'T';
            serial_str[2] = FW_BUILD_TAG[0];      // build marker
            serial_str[3] = FW_BUILD_TAG[1];
            for (int i = 0; i < 2; i++) {
                serial_str[4 + i * 2]     = hexd[(id.id[i] >> 4) & 0xF];
                serial_str[4 + i * 2 + 1] = hexd[id.id[i] & 0xF];
            }
            serial_str[8] = 0;
            s = serial_str;
            break;
        }
        default: return NULL;
        }
        chr_count = (uint8_t)strlen(s);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = s[i];
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}

// --- debug counters, readable via vendor request 0xFE -----------------------
// Temporary instrumentation for OUT-path bring-up. Remove once it works.
typedef struct {
    uint16_t open_a;     // ftdi_drv_open called for interface 0
    uint16_t arm_call;   // arm_out() entered
    uint16_t arm_ok;     // usbd_edpt_xfer on EPA_OUT returned true
    uint16_t cb_out;     // xfer_cb fired for EPA_OUT
    uint16_t cb_bytes;   // total bytes delivered by those callbacks
    uint16_t feed_in;    // bytes handed to mpsse_feed
    uint16_t feed_used;  // bytes mpsse_feed consumed
    uint16_t resp_now;   // current response-ring depth
} dbg_t;
static dbg_t dbg;

// ---------------------------------------------------------------------------
// FTDI vendor control requests (bmRequestType 0x40 / 0xC0, recipient = device)
// ---------------------------------------------------------------------------
#define SIO_RESET             0x00
#define SIO_MODEM_CTRL        0x01
#define SIO_SET_FLOW_CTRL     0x02
#define SIO_SET_BAUD_RATE     0x03
#define SIO_SET_DATA          0x04
#define SIO_POLL_MODEM_STATUS 0x05
#define SIO_SET_EVENT_CHAR    0x06
#define SIO_SET_ERROR_CHAR    0x07
#define SIO_SET_LATENCY_TIMER 0x09
#define SIO_GET_LATENCY_TIMER 0x0A
#define SIO_SET_BITMODE       0x0B
#define SIO_READ_PINS         0x0C
#define SIO_READ_EEPROM       0x90

static uint8_t latency_ms = 16;
static uint8_t bitmode    = 0;

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request)
{
    if (stage != CONTROL_STAGE_SETUP) return true;

    // wIndex selects the channel: 1 = interface A (JTAG), 2 = interface B.
    // Without this check, ftdi_sio bound to interface B resets our MPSSE state.
    bool const for_a = (request->wIndex & 0xFF) != 2;

    switch (request->bRequest) {
    case SIO_RESET:
        // wValue: 0 = reset SIO, 1 = purge RX, 2 = purge TX
        if (for_a) mpsse_reset();
        return tud_control_status(rhport, request);

    case SIO_SET_BITMODE:
        if (!for_a) return tud_control_status(rhport, request);
        bitmode = (uint8_t)(request->wValue >> 8);
        if (bitmode == 0x00 || bitmode == 0x02) mpsse_reset();
        return tud_control_status(rhport, request);

    case SIO_SET_LATENCY_TIMER:
        if (!for_a) return tud_control_status(rhport, request);
        latency_ms = (uint8_t)(request->wValue & 0xFF);
        if (latency_ms == 0) latency_ms = 1;
        return tud_control_status(rhport, request);

    case SIO_GET_LATENCY_TIMER:
        return tud_control_xfer(rhport, request, &latency_ms, 1);

    case SIO_POLL_MODEM_STATUS: {
        static uint8_t st[2] = { FTDI_MODEM_STATUS, FTDI_LINE_STATUS };
        return tud_control_xfer(rhport, request, st, 2);
    }

    case SIO_READ_PINS: {
        static uint8_t p = 0;
        return tud_control_xfer(rhport, request, &p, 1);
    }

    case 0xFE: {   // DEBUG: read bring-up counters
        dbg.resp_now = (uint16_t)mpsse_resp_count();
        return tud_control_xfer(rhport, request, &dbg, sizeof(dbg));
    }

    case 0xFD: {   // DEBUG: which ADBUS/ACBUS bits has the host driven?
        static uint8_t g[8];
        mpsse_gpio_debug(g);
        return tud_control_xfer(rhport, request, g, sizeof(g));
    }

    case 0xFC: {   // DEBUG: read opcode trace at offset wValue
        static uint8_t t[64];
        uint16_t n = mpsse_trace_read(request->wValue, t, sizeof(t));
        return tud_control_xfer(rhport, request, t, n);
    }

    case 0xFB:     // DEBUG: clear opcode trace
        mpsse_trace_clear();
        return tud_control_status(rhport, request);

    case 0xFA: {   // DEBUG: trace length
        static uint8_t l[2];
        uint16_t n = mpsse_trace_len();
        l[0] = (uint8_t)(n & 0xFF); l[1] = (uint8_t)(n >> 8);
        return tud_control_xfer(rhport, request, l, 2);
    }

    case SIO_READ_EEPROM: {
        // Blank EEPROM. Returning zeros keeps libftdi's eeprom read happy.
        static uint8_t z[2] = { 0, 0 };
        return tud_control_xfer(rhport, request, z, 2);
    }

    // Accepted and ignored -- but they must not stall.
    case SIO_MODEM_CTRL:
    case SIO_SET_FLOW_CTRL:
    case SIO_SET_BAUD_RATE:
    case SIO_SET_DATA:
    case SIO_SET_EVENT_CHAR:
    case SIO_SET_ERROR_CHAR:
        return tud_control_status(rhport, request);

    default:
        // Stall rather than tud_control_status(): for an IN request the status
        // stage runs in the opposite direction, so answering with one leaves
        // the host NAK-looping until timeout instead of failing cleanly.
        return false;
    }
}

// ---------------------------------------------------------------------------
// Custom class driver: raw endpoint control so packet boundaries are ours
// ---------------------------------------------------------------------------
static uint8_t  out_buf[EP_SIZE];
static uint16_t out_len, out_pos;
static bool     out_pending;

static uint8_t  in_buf[EP_SIZE];
static bool     in_busy;
static uint32_t last_in_ms;

// Channel B. Declared in the descriptors so we look like a real FT2232H, and
// now actually answered: a genuine part always responds on channel B, and a
// silent endpoint makes any host read time out. Data in is discarded; reads
// get the same 2-byte status packets an idle UART would produce.
static uint8_t  outB_buf[EP_SIZE];
static uint8_t  inB_buf[EP_SIZE];
static bool     inB_busy;
static uint32_t last_inB_ms;


static void ftdi_usb_task_b(void);

static inline uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }


static void arm_out(void)
{
    dbg.arm_call++;
    if (out_pending) return;
    if (usbd_edpt_claim(0, EPA_OUT)) {
        if (usbd_edpt_xfer(0, EPA_OUT, out_buf, EP_SIZE)) dbg.arm_ok++;
        else usbd_edpt_release(0, EPA_OUT);
    }
}

static void arm_outB(void)
{
#if FTDI_SINGLE_CHANNEL
    return;
#else
    if (usbd_edpt_claim(0, EPB_OUT)) {
        if (!usbd_edpt_xfer(0, EPB_OUT, outB_buf, EP_SIZE))
            usbd_edpt_release(0, EPB_OUT);
    }
#endif
}

static void ftdi_drv_init(void) { }

static void ftdi_drv_reset(uint8_t rhport)
{
    (void)rhport;
    out_pending = false; out_len = out_pos = 0;
    in_busy = false; last_in_ms = now_ms();
    inB_busy = false; last_inB_ms = now_ms();
    mpsse_reset();
}

static uint16_t ftdi_drv_open(uint8_t rhport, tusb_desc_interface_t const *itf,
                              uint16_t max_len)
{
    if (itf->bInterfaceClass != 0xFF) return 0;

    uint16_t const need = (uint16_t)(sizeof(tusb_desc_interface_t) + 2 * 7);
    TU_VERIFY(max_len >= need, 0);

    uint8_t const *p = tu_desc_next(itf);
    for (int i = 0; i < 2; i++) {
        TU_ASSERT(TUSB_DESC_ENDPOINT == tu_desc_type(p), 0);
        TU_ASSERT(usbd_edpt_open(rhport, (tusb_desc_endpoint_t const *)p), 0);
        p = tu_desc_next(p);
    }

    if (itf->bInterfaceNumber == 0) { dbg.open_a++; arm_out(); }
#if !FTDI_SINGLE_CHANNEL
    else                           { arm_outB(); }
#endif
    return need;
}

static bool ftdi_drv_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                     tusb_control_request_t const *request)
{
    (void)rhport; (void)stage; (void)request;
    return true;
}

static bool ftdi_drv_xfer_cb(uint8_t rhport, uint8_t ep, xfer_result_t result,
                             uint32_t xferred)
{
    (void)rhport; (void)result;

    if (ep == EPA_OUT) {
        dbg.cb_out++;
        dbg.cb_bytes = (uint16_t)(dbg.cb_bytes + xferred);
        out_len = (uint16_t)xferred;
        out_pos = 0;
        out_pending = (out_len > 0);
        if (!out_pending) arm_out();     // ZLP: just re-arm
    } else if (ep == EPA_IN) {
        in_busy = false;
    } else if (ep == EPB_OUT) {
        // interface B not implemented; swallow and re-arm
        if (usbd_edpt_claim(0, EPB_OUT))
            if (!usbd_edpt_xfer(0, EPB_OUT, out_buf, 0))
                usbd_edpt_release(0, EPB_OUT);
    }
    return true;
}

static const usbd_class_driver_t ftdi_driver[] = {{
#if CFG_TUSB_DEBUG >= 2
    .name = "FTDI",
#endif
    .init            = ftdi_drv_init,
    .reset           = ftdi_drv_reset,
    .open            = ftdi_drv_open,
    .control_xfer_cb = ftdi_drv_control_xfer_cb,
    .xfer_cb         = ftdi_drv_xfer_cb,
    .sof             = NULL
}};

usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *count)
{
    *count = 1;
    return ftdi_driver;
}

// ---------------------------------------------------------------------------
// Main-loop pump
// ---------------------------------------------------------------------------
void ftdi_usb_task(void)
{
    if (!tud_mounted()) return;

    ftdi_usb_task_b();   // first: channel A has several early returns below

    // 0. Advance any read-only shift that stalled on a full response ring.
    mpsse_pump();

    // 1. Drain the OUT packet through the MPSSE interpreter.
    if (out_pending) {
        dbg.feed_in = (uint16_t)(dbg.feed_in + (out_len - out_pos));
        size_t used = mpsse_feed(out_buf + out_pos, out_len - out_pos);
        dbg.feed_used = (uint16_t)(dbg.feed_used + used);
        out_pos = (uint16_t)(out_pos + used);
        if (out_pos >= out_len) {
            out_pending = false;
            arm_out();
        }
        // else: response ring is full -- fall through, drain, retry next pass
    }

    // 2. Push responses, with the mandatory 2-byte status prefix.
    if (in_busy) return;

    size_t avail = mpsse_resp_count();
    bool   immediate = mpsse_send_immediate();
    bool   due = (uint32_t)(now_ms() - last_in_ms) >= latency_ms;

    if (avail == 0 && !immediate && !due) return;

    // Claim BEFORE draining the ring: mpsse_resp_read() is destructive, so
    // doing it first threw the bytes away whenever the claim failed.
    if (!usbd_edpt_claim(0, EPA_IN)) return;

    in_buf[0] = FTDI_MODEM_STATUS;
    in_buf[1] = FTDI_LINE_STATUS;
    uint16_t n = (uint16_t)(2 + mpsse_resp_read(in_buf + 2, EP_SIZE - 2));

    if (usbd_edpt_xfer(0, EPA_IN, in_buf, n)) {
        in_busy = true;
        last_in_ms = now_ms();
    } else {
        usbd_edpt_release(0, EPA_IN);
    }
}

// Keep channel B alive. Without this any host read on 0x83 never completes and
// surfaces as a USB timeout, even though the JTAG channel is working fine.
static void ftdi_usb_task_b(void)
{
#if FTDI_SINGLE_CHANNEL
    return;                 // no channel B on an FT232H
#else
    if (inB_busy) return;
    if ((uint32_t)(now_ms() - last_inB_ms) < latency_ms) return;

    if (!usbd_edpt_claim(0, EPB_IN)) return;

    inB_buf[0] = FTDI_MODEM_STATUS;
    inB_buf[1] = FTDI_LINE_STATUS;

    if (usbd_edpt_xfer(0, EPB_IN, inB_buf, 2)) {
        inB_busy = true;
        last_inB_ms = now_ms();
    } else {
        usbd_edpt_release(0, EPB_IN);
    }
#endif
}