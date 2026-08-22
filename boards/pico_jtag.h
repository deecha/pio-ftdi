// Raspberry Pi Pico (RP2040) as a JTAG adapter.
//
// Not named pico.h on purpose: this directory is searched before the SDK's own
// boards/, so that name would shadow the SDK header and lose everything it
// defines.
//
// Build with:  cmake -DRP2040=1 -DPICO_BOARD=pico_jtag ..
#ifndef _BOARDS_PICO_JTAG_H
#define _BOARDS_PICO_JTAG_H

// GPIO numbers, not physical pin numbers.
//   GPIO1 = physical pin 2   TCK
//   GPIO2 = physical pin 4   TDI
//   GPIO3 = physical pin 5   TDO
//   GPIO4 = physical pin 6   TMS
#ifndef PIN_TCK
#define PIN_TCK   1
#define PIN_TDI   2
#define PIN_TDO   3
#define PIN_TMS   4
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif
#ifndef PICO_BOOT_STAGE2_CHOOSE_W25Q080
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#endif
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

#endif
