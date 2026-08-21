#include "jtag_pio.h"
#include "board_config.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "jtag.pio.h"

#define JPIO pio0
#define JSM  0

static uint     prog_off;
static enum { MODE_NONE, MODE_DATA, MODE_TMS } mode = MODE_NONE;
static bool     s_tck = false, s_tdi = false, s_tms = false;

// ---------------------------------------------------------------------------
// PINCTRL swap
// ---------------------------------------------------------------------------
static inline void pinctrl_set(unsigned out_pin, unsigned set_pin)
{
    JPIO->sm[JSM].pinctrl =
          (1u              << PIO_SM0_PINCTRL_SIDESET_COUNT_LSB)
        | (PREL(PIN_TCK)   << PIO_SM0_PINCTRL_SIDESET_BASE_LSB)
        | (PREL(out_pin)   << PIO_SM0_PINCTRL_OUT_BASE_LSB)
        | (1u              << PIO_SM0_PINCTRL_OUT_COUNT_LSB)
        | (PREL(PIN_TDO)   << PIO_SM0_PINCTRL_IN_BASE_LSB)
        | (PREL(set_pin)   << PIO_SM0_PINCTRL_SET_BASE_LSB)
        | (1u              << PIO_SM0_PINCTRL_SET_COUNT_LSB);
}

static void mode_data(void)
{
    if (mode == MODE_DATA) return;
    pinctrl_set(PIN_TDI, PIN_TMS);
    mode = MODE_DATA;
}

static void mode_tms(bool tdi_level)
{
    pinctrl_set(PIN_TMS, PIN_TDI);
    mode = MODE_TMS;
    // SM is stalled at the leading `pull block`; injecting a SET is safe here
    // and it resumes the stalled pull afterwards.
    pio_sm_exec(JPIO, JSM, pio_encode_set(pio_pins, tdi_level ? 1 : 0));
    s_tdi = tdi_level;
}

// ---------------------------------------------------------------------------
void jtag_pio_init(void)
{
    // RP2350 PIO sees a 32-GPIO window; GPIO30..33 need base 16.
    // RP2350B only; RP2040 has no GPIO window and no such API.
#if PICO_PIO_USE_GPIO_BASE
    pio_set_gpio_base(JPIO, PIO_GPIO_BASE);
#endif

    prog_off = pio_add_program(JPIO, &jtag_shift_program);

    pio_gpio_init(JPIO, PIN_TCK);
    pio_gpio_init(JPIO, PIN_TDI);
    pio_gpio_init(JPIO, PIN_TMS);
    pio_gpio_init(JPIO, PIN_TDO);

    // Bias TDO so an undriven line reads a known value (see TDO_PULL_UP).
    gpio_set_pulls(PIN_TDO, TDO_PULL_UP ? true : false, TDO_PULL_UP ? false : true);

    pio_sm_set_consecutive_pindirs(JPIO, JSM, PIN_TCK, 1, true);
    pio_sm_set_consecutive_pindirs(JPIO, JSM, PIN_TDI, 1, true);
    pio_sm_set_consecutive_pindirs(JPIO, JSM, PIN_TMS, 1, true);
    pio_sm_set_consecutive_pindirs(JPIO, JSM, PIN_TDO, 1, false);

    // sm_config_set_*_pins() take ABSOLUTE GPIO numbers: pio_sm_set_config()
    // subtracts the GPIO base itself when PICO_PIO_USE_GPIO_BASE == 1.
    // Only the direct pinctrl writes in pinctrl_set() use relative values.
    pio_sm_config c = jtag_shift_program_get_default_config(prog_off);
    sm_config_set_sideset_pins(&c, PIN_TCK);
    sm_config_set_out_pins(&c, PIN_TDI, 1);
    sm_config_set_in_pins(&c, PIN_TDO);
    sm_config_set_set_pins(&c, PIN_TMS, 1);

    // LSB-first both ways; explicit pull/push, so no autopull/autopush.
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_clkdiv(&c, 16.0f);        // ~2.3 MHz TCK until host sets 0x86

    pio_sm_init(JPIO, JSM, prog_off, &c);
    mode = MODE_DATA;
    pio_sm_set_enabled(JPIO, JSM, true);

    jtag_set_static(false, false, false);
}

void jtag_pio_reset(void)
{
    pio_sm_set_enabled(JPIO, JSM, false);
    pio_sm_clear_fifos(JPIO, JSM);
    pio_sm_restart(JPIO, JSM);
    pio_sm_clkdiv_restart(JPIO, JSM);
    pio_sm_exec(JPIO, JSM, pio_encode_jmp(prog_off));
    mode = MODE_NONE;                 // force PINCTRL reapply on next shift
    pio_sm_set_enabled(JPIO, JSM, true);
}

// ---------------------------------------------------------------------------
uint32_t jtag_shift_data(unsigned nbits, uint32_t data)
{
    if (nbits == 0 || nbits > 32) return 0;
    mode_data();
    pio_sm_put_blocking(JPIO, JSM, nbits - 1);
    pio_sm_put_blocking(JPIO, JSM, data);
    uint32_t r = pio_sm_get_blocking(JPIO, JSM);
    s_tdi = (data >> (nbits - 1)) & 1;
    return r >> (32 - nbits);
}

uint32_t jtag_shift_tms(unsigned nbits, uint32_t tms, bool tdi_level)
{
    if (nbits == 0 || nbits > 32) return 0;
    mode_tms(tdi_level);
    pio_sm_put_blocking(JPIO, JSM, nbits - 1);
    pio_sm_put_blocking(JPIO, JSM, tms);
    uint32_t r = pio_sm_get_blocking(JPIO, JSM);
    s_tms = (tms >> (nbits - 1)) & 1;
    return r >> (32 - nbits);
}

void jtag_clock_only(uint32_t nbits)
{
    uint32_t fill = s_tdi ? 0xFFFFFFFFu : 0u;
    while (nbits) {
        unsigned n = nbits > 32 ? 32 : nbits;
        jtag_shift_data(n, fill);
        nbits -= n;
    }
}

// ---------------------------------------------------------------------------
void jtag_set_divisor(uint16_t divisor, bool div_by_5)
{
    uint32_t base = div_by_5 ? 12000000u : 60000000u;
    uint32_t tck  = base / (2u * ((uint32_t)divisor + 1u));

    if (tck > JTAG_MAX_TCK_HZ) tck = JTAG_MAX_TCK_HZ;
    if (tck < JTAG_MIN_TCK_HZ) tck = JTAG_MIN_TCK_HZ;

    float div = (float)clock_get_hz(clk_sys) / (12.0f * (float)tck);
    if (div < 1.0f)     div = 1.0f;
    if (div > 65535.0f) div = 65535.0f;

    pio_sm_set_clkdiv(JPIO, JSM, div);
    pio_sm_clkdiv_restart(JPIO, JSM);
}

bool jtag_tdo_level(void)
{
    return gpio_get(PIN_TDO);   // SIO input works regardless of pin function
}

void jtag_set_static(bool tck, bool tdi, bool tms)
{
    // TCK idles low out of the program; TDI/TMS are driven via the SET group.
    (void)tck;
    if (tms != s_tms) {
        pinctrl_set(PIN_TDI, PIN_TMS);
        pio_sm_exec(JPIO, JSM, pio_encode_set(pio_pins, tms ? 1 : 0));
        mode = MODE_DATA;
        s_tms = tms;
    }
    if (tdi != s_tdi) {
        pinctrl_set(PIN_TMS, PIN_TDI);
        pio_sm_exec(JPIO, JSM, pio_encode_set(pio_pins, tdi ? 1 : 0));
        mode = MODE_TMS;
        s_tdi = tdi;
    }
    s_tck = false;
}

void jtag_get_static(bool *tck, bool *tdi, bool *tms)
{
    if (tck) *tck = s_tck;
    if (tdi) *tdi = s_tdi;
    if (tms) *tms = s_tms;
}