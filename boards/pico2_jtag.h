// Raspberry Pi Pico 2 (RP2350A) as a JTAG adapter.
//
// Not named pico2.h on purpose: this directory is searched before the SDK's
// own boards/, so that name would shadow the SDK header.
//
// Build with:  cmake -DPICO_BOARD=pico2_jtag ..
//
// RP2350A is the 30-GPIO variant, so PICO_RP2350A stays at its default of 1
// and GPIO30+ does not exist here. The PIO GPIO window resolves to 0.
#ifndef _BOARDS_PICO2_JTAG_H
#define _BOARDS_PICO2_JTAG_H

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
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
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
