// Zen board: RP2350B (QFN-80, 48 GPIO) + Efinix Trion T4/T8
#ifndef _BOARDS_ZEN_H
#define _BOARDS_ZEN_H

#define PICO_RP2350A 0          // 0 => B variant, 48 GPIO

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
