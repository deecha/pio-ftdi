// Vicharak Zen: RP2350B + Efinix Trion T4F81, JTAG wired MCU-to-FPGA.
//
// A board file can carry the pinout as well as the chip variant: the SDK
// includes it via pico.h, which board_config.h pulls in before its own
// #ifndef defaults. Keep the guards so -DJTAG_PINS still wins.
#ifndef _BOARDS_ZEN_H
#define _BOARDS_ZEN_H

#define PICO_RP2350A 0          // B variant, 48 GPIO -- required for GPIO30+

// GPIO30 = pin 38 = TCK, 31 = 39 = TDI, 32 = 40 = TDO, 33 = 42 = TMS
#ifndef PIN_TCK
#define PIN_TCK   30
#define PIN_TDI   31
#define PIN_TDO   32
#define PIN_TMS   33
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

#endif
