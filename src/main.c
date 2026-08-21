#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "board_config.h"
#include "jtag_pio.h"
#include "mpsse.h"

void ftdi_usb_task(void);

int main(void)
{
    set_sys_clock_khz(JTAG_SYS_CLK_KHZ, true);

    jtag_pio_init();
    mpsse_reset();

    tusb_init();

    while (true) {
        tud_task();
        ftdi_usb_task();
    }
}
